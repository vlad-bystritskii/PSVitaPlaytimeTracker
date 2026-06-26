/* Vita Trophy Dump — read on-console trophy UNLOCK STATE and write it as JSON.
 *
 * Per-game unlock state lives in ur0:user/00/trophy/data/<npcommid>/TRPTITLE.DAT,
 * encrypted per-console (sce_pfs + sce_sys/sealedkey, F00D) — unreadable raw.
 * We mount that folder with the trophymount kernel plugin (SceAppMgr mount-id
 * 0x12F): the OS unseals the key and exposes the DECRYPTED folder at a mount
 * point, which we read with ordinary sceIo. Then we parse TRPTITLE.DAT:
 *   num trophies   : byte @ 0xFF
 *   block2 base    : BE32 @ 0x164, per-trophy stride 0x60
 *     +0x17 (u8)   : unlocked (1) / locked (0)
 *     +0x20 (BE64) : unlock time, microseconds since 0001-01-01 UTC
 * (offsets from AnalogMan151/PSVTrophyIsGreat, corroborated by ONElua).
 *
 * Output ux0:data/VitaPlaytime/trophies.json, one object per line:
 *   {"npcommid":"NPWR06254_00","count":29,"unlocked":[{"id":1,"t":1700000000}]}
 * plus ux0:data/VitaPlaytime/trophy_dump_log.txt with per-step diagnostics.
 * The HA add-on pulls trophies.json over FTP and merges it with the trophy
 * names/grades/icons it reads from the plaintext conf/<npcommid>/TROP.SFM.
 */
#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "trophymount.h"

#define DATA_DIR "ur0:user/00/trophy/data"
#define OUT_DIR  "ux0:data/VitaPlaytime"
#define JSON_OUT OUT_DIR "/trophies.json"
#define LOG_OUT  OUT_DIR "/trophy_dump_log.txt"
#define ICON_DIR OUT_DIR "/icons"  /* per-set per-trophy PNGs the add-on FTP-pulls */

/* Unix epoch (1970) expressed in the trophy clock (microseconds since 0001). */
#define EPOCH_1970_US 62135596800000000ULL

static SceUID g_log = -1;
static FILE  *g_json = NULL;

static void logln(const char *s) {
	if (g_log >= 0) { sceIoWrite(g_log, s, strlen(s)); sceIoWrite(g_log, "\n", 1); }
}
static void logfmt(const char *fmt, ...) {
	char buf[512];
	va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
	logln(buf);
}

static uint32_t be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) {
	return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

/* Read a whole (small) file into buf. Returns bytes read or <0. */
static int read_all(const char *path, uint8_t *buf, int cap) {
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	int total = 0, r;
	while (total < cap && (r = sceIoRead(fd, buf + total, cap - total)) > 0)
		total += r;
	sceIoClose(fd);
	return total;
}

/* Write a whole buffer to path (create/truncate). Returns bytes written or <0. */
static int write_all(const char *path, const uint8_t *buf, int len) {
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return fd;
	int total = 0, w;
	while (total < len && (w = sceIoWrite(fd, buf + total, len - total)) > 0)
		total += w;
	sceIoClose(fd);
	return total;
}

/* List a mount point's root into the log (diagnostic when TRPTITLE.DAT isn't found). */
static void log_listing(const char *mp) {
	char dir[32];
	snprintf(dir, sizeof(dir), "%s/", mp);
	SceUID d = sceIoDopen(dir);
	if (d < 0) { logfmt("    Dopen %s -> 0x%08X", dir, d); return; }
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	while (sceIoDread(d, &e) > 0) {
		logfmt("    entry: %s (%lld)", e.d_name, (long long)e.d_stat.st_size);
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
}

static uint8_t g_trp[40960];
static uint8_t g_icon[512 * 1024];  /* one trophy PNG at a time */

/* Copy decrypted per-trophy icons (TROP<digits>.PNG, e.g. TROP000.PNG) out of the
 * mounted folder to ux0:data/VitaPlaytime/icons/<npcommid>/<id>.png — the id is the
 * trophy number (matches TROP.SFM ids and the add-on's /trophy-icon route). The
 * add-on FTP-pulls these. Best-effort; never fails the dump. Returns count copied. */
static int copy_icons(const char *mp, const char *name) {
	sceIoMkdir(ICON_DIR, 0777);
	char destdir[320];
	snprintf(destdir, sizeof(destdir), "%s/%s", ICON_DIR, name);
	sceIoMkdir(destdir, 0777);

	char dir[32];
	snprintf(dir, sizeof(dir), "%s/", mp);
	SceUID d = sceIoDopen(dir);
	if (d < 0) { logfmt("    icons: Dopen %s -> 0x%08X", dir, d); return 0; }

	int copied = 0;
	SceIoDirent e;
	memset(&e, 0, sizeof(e));
	while (sceIoDread(d, &e) > 0) {
		const char *nm = e.d_name;
		if ((nm[0] == 'T' || nm[0] == 't') && (nm[1] == 'R' || nm[1] == 'r') &&
		    (nm[2] == 'O' || nm[2] == 'o') && (nm[3] == 'P' || nm[3] == 'p')) {
			int id = 0, k = 4, has = 0;
			while (nm[k] >= '0' && nm[k] <= '9') { id = id * 10 + (nm[k] - '0'); has = 1; k++; }
			if (has && nm[k] == '.' &&
			    (nm[k+1] == 'P' || nm[k+1] == 'p') && (nm[k+2] == 'N' || nm[k+2] == 'n') &&
			    (nm[k+3] == 'G' || nm[k+3] == 'g') && nm[k+4] == 0) {
				char src[384], dst[384];
				snprintf(src, sizeof(src), "%s/%s", mp, nm);
				snprintf(dst, sizeof(dst), "%s/%d.png", destdir, id);
				int rn = read_all(src, g_icon, sizeof(g_icon));
				if (rn > 0 && write_all(dst, g_icon, rn) == rn) copied++;
			}
		}
		memset(&e, 0, sizeof(e));
	}
	sceIoDclose(d);
	return copied;
}

static void dump_one(const char *name) {
	char path[256], mp[32], file[64];
	char klic[16];
	memset(klic, 0, sizeof(klic));
	memset(mp, 0, sizeof(mp));

	snprintf(path, sizeof(path), "%s/%s/", DATA_DIR, name);

	ShellMountIdArgs a;
	a.process_titleid = "VPTD00001";
	a.path = path;
	a.desired_mount_point = NULL;
	a.klicensee = klic;
	a.mount_point = mp;

	int r = -1;
	int ids[] = { 0x12F, 0x12E };
	for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		a.id = ids[i];
		r = trophyKernelMountById(&a);
		if (r >= 0) break;
	}
	if (r < 0) { logfmt("%s: mount failed 0x%08X", name, r); return; }
	logfmt("%s: mounted at '%s' (0x%08X)", name, mp, r);

	snprintf(file, sizeof(file), "%s/TRPTITLE.DAT", mp);
	int n = read_all(file, g_trp, sizeof(g_trp));
	if (n < 0x200) {
		logfmt("%s: TRPTITLE.DAT read=%d — listing mount:", name, n);
		log_listing(mp);
		sceAppMgrUmount(mp);
		return;
	}

	int num = g_trp[0xFF];
	uint32_t b2 = be32(g_trp + 0x164);
	int unlocked_total = 0;

	fprintf(g_json, "{\"npcommid\":\"%s\",\"count\":%d,\"unlocked\":[", name, num);
	int first = 1;
	for (int t = 0; t < num && t < 128; t++) {
		uint32_t off = b2 + (uint32_t)t * 0x60;
		if (off + 0x28 > (uint32_t)n) break;
		if (g_trp[off + 0x17] != 1) continue;  /* locked */
		unlocked_total++;
		uint64_t micros = be64(g_trp + off + 0x20);
		unsigned long long unix_s =
			(micros > EPOCH_1970_US) ? (micros - EPOCH_1970_US) / 1000000ULL : 0ULL;
		fprintf(g_json, "%s{\"id\":%d,\"t\":%llu}", first ? "" : ",", t, unix_s);
		first = 0;
	}
	fprintf(g_json, "]}\n");
	fflush(g_json);

	int icons = copy_icons(mp, name);
	logfmt("%s: OK count=%d unlocked=%d icons=%d", name, num, unlocked_total, icons);
	sceAppMgrUmount(mp);
}

int main(void) {
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir(OUT_DIR, 0777);

	g_log = sceIoOpen(LOG_OUT, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	logln("== Vita Trophy Dump (mount) ==");

	g_json = fopen(JSON_OUT, "w");
	if (!g_json) { logln("FATAL: cannot open trophies.json"); goto done; }

	SceUID d = sceIoDopen(DATA_DIR);
	if (d < 0) { logfmt("FATAL: Dopen %s -> 0x%08X", DATA_DIR, d); goto done; }

	int games = 0;
	SceIoDirent ent;
	memset(&ent, 0, sizeof(ent));
	while (sceIoDread(d, &ent) > 0) {
		if (SCE_S_ISDIR(ent.d_stat.st_mode) && strncmp(ent.d_name, "NPWR", 4) == 0) {
			games++;
			dump_one(ent.d_name);
		}
		memset(&ent, 0, sizeof(ent));
	}
	sceIoDclose(d);
	logfmt("done: %d trophy set(s) scanned", games);

done:
	if (g_json) fclose(g_json);
	logln("== finished ==");
	if (g_log >= 0) sceIoClose(g_log);

	sceKernelDelayThread(1000 * 1000);
	sceKernelExitProcess(0);
	return 0;
}
