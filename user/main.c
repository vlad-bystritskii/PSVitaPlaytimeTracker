/*
 * VitaPlaytimeTracker — user module (playtime_u.suprx)
 *
 * Loaded into SceShell (*main). A background thread drains the kernel module's
 * session queue (ux0:data/VitaPlaytime/pending.jsonl), resolves each game's display
 * name from its param.sfo, and POSTs the session to the Playtime Collector add-on's
 * /ingest endpoint over plain HTTP. Sessions that fail to send stay queued and retry
 * (this is the offline buffer). Networking lives here, in user mode, on purpose.
 *
 * License: GPLv3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>

#define PT_DIR     "ux0:data/VitaPlaytime"
#define PT_CONFIG  PT_DIR "/config.txt"
#define PT_PENDING PT_DIR "/pending.jsonl"
#define PT_SENDING PT_DIR "/sending.jsonl"

#define POLL_SEC      30
#define QUEUE_MAX     (64 * 1024)
#define NET_POOL_SZ   (16 * 1024)
#define HTTP_POOL_SZ  (40 * 1024)

typedef struct {
	char host[96];
	int  port;
	char token[128];
	char account[64];
} Config;

static char net_pool[NET_POOL_SZ];
static int  net_ready;

/* ---- tiny string helpers --------------------------------------------- */

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

/* read an entire file into buf (NUL-terminated). returns length, or -1. */
static int read_file(const char *path, char *buf, int max)
{
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return -1;
	int n = sceIoRead(fd, buf, max - 1);
	sceIoClose(fd);
	if (n < 0) return -1;
	buf[n] = 0;
	return n;
}

/* ---- config ---------------------------------------------------------- */

static int load_config(Config *c)
{
	memset(c, 0, sizeof(*c));
	c->port = 3301;
	strcpy(c->account, "vita");

	char buf[2048];
	if (read_file(PT_CONFIG, buf, sizeof(buf)) < 0)
		return -1;

	char *save = NULL;
	for (char *ln = strtok_r(buf, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
		char *line = trim(ln);
		if (line[0] == 0 || line[0] == '#') continue;
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		char *key = trim(line);
		char *val = trim(eq + 1);
		if      (!strcmp(key, "ha_host"))    strncpy(c->host, val, sizeof(c->host) - 1);
		else if (!strcmp(key, "ha_port"))    c->port = atoi(val);
		else if (!strcmp(key, "auth_token")) strncpy(c->token, val, sizeof(c->token) - 1);
		else if (!strcmp(key, "account"))    strncpy(c->account, val, sizeof(c->account) - 1);
	}
	return c->host[0] ? 0 : -1;
}

/* ---- param.sfo title resolution -------------------------------------- */

typedef struct {
	uint32_t magic;            /* "\0PSF" = 0x46535000 */
	uint32_t version;
	uint32_t key_table_start;
	uint32_t data_table_start;
	uint32_t num_entries;
} SfoHeader;

typedef struct {
	uint16_t key_offset;
	uint16_t param_fmt;
	uint32_t param_len;
	uint32_t param_max_len;
	uint32_t data_offset;
} SfoEntry;

/* Fill out[] with the game's display TITLE; fall back to the title id. */
static void resolve_title(const char *title_id, char *out, int outsz)
{
	snprintf(out, outsz, "%s", title_id);   /* fallback */

	char path[128];
	snprintf(path, sizeof(path), "ux0:app/%s/sce_sys/param.sfo", title_id);

	static char sfo[8192];
	int n = read_file(path, sfo, sizeof(sfo));
	if (n < (int)sizeof(SfoHeader)) return;

	SfoHeader *h = (SfoHeader *)sfo;
	if (h->magic != 0x46535000) return;

	SfoEntry *e = (SfoEntry *)(sfo + sizeof(SfoHeader));
	for (uint32_t i = 0; i < h->num_entries; i++) {
		const char *key = sfo + h->key_table_start + e[i].key_offset;
		if (h->key_table_start + e[i].key_offset >= (uint32_t)n) continue;
		if (strcmp(key, "TITLE") == 0) {
			const char *val = sfo + h->data_table_start + e[i].data_offset;
			if (h->data_table_start + e[i].data_offset < (uint32_t)n) {
				snprintf(out, outsz, "%s", val);
			}
			return;
		}
	}
}

/* ---- json line parsing (from the queue) ------------------------------ */

/* pull the value of "key":"..." or "key":N out of a queue line */
static int json_str(const char *line, const char *key, char *out, int outsz)
{
	char pat[32];
	snprintf(pat, sizeof(pat), "\"%s\":\"", key);
	const char *p = strstr(line, pat);
	if (!p) return -1;
	p += strlen(pat);
	int i = 0;
	while (*p && *p != '"' && i < outsz - 1) out[i++] = *p++;
	out[i] = 0;
	return 0;
}

static long json_num(const char *line, const char *key)
{
	char pat[32];
	snprintf(pat, sizeof(pat), "\"%s\":", key);
	const char *p = strstr(line, pat);
	if (!p) return -1;
	return atol(p + strlen(pat));
}

/* escape a title for embedding in our JSON body */
static void json_escape(const char *in, char *out, int outsz)
{
	int o = 0;
	for (int i = 0; in[i] && o < outsz - 2; i++) {
		char ch = in[i];
		if (ch == '"' || ch == '\\') out[o++] = '\\';
		if ((unsigned char)ch < 0x20) ch = ' ';
		out[o++] = ch;
	}
	out[o] = 0;
}

/* ---- networking ------------------------------------------------------ */

static void ensure_net(void)
{
	if (net_ready) return;

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	SceNetInitParam p;
	p.memory = net_pool;
	p.size   = NET_POOL_SZ;
	p.flags  = 0;
	sceNetInit(&p);            /* already-inited by SceShell -> error ignored */
	sceNetCtlInit();

	sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
	sceHttpInit(HTTP_POOL_SZ);

	net_ready = 1;
}

static int net_connected(void)
{
	int state = 0;
	if (sceNetCtlInetGetState(&state) < 0) return 0;
	return state == SCE_NETCTL_STATE_CONNECTED;
}

/* POST one session. returns 0 on a 2xx, -1 otherwise. */
static int post_session(const Config *c, const char *title_id,
			const char *title, uint32_t seconds)
{
	char url[192];
	snprintf(url, sizeof(url), "http://%s:%d/ingest", c->host, c->port);

	char title_esc[256];
	json_escape(title, title_esc, sizeof(title_esc));

	char body[640];
	int blen = snprintf(body, sizeof(body),
		"{\"platform\":\"psvita\",\"account\":\"%s\",\"titleId\":\"%s\","
		"\"title\":\"%s\",\"seconds\":%u}",
		c->account, title_id, title_esc, seconds);
	if (blen <= 0) return -1;

	int rc = -1, status = 0;
	int tpl = sceHttpCreateTemplate("VitaPlaytime/1.0", SCE_HTTP_VERSION_1_1, 1);
	if (tpl < 0) return -1;
	sceHttpSetConnectTimeOut(tpl, 5 * 1000 * 1000);
	int conn = sceHttpCreateConnectionWithURL(tpl, url, 0);
	if (conn >= 0) {
		int req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_POST, url, blen);
		if (req >= 0) {
			sceHttpAddRequestHeader(req, "Content-Type", "application/json",
						SCE_HTTP_HEADER_ADD);
			if (c->token[0])
				sceHttpAddRequestHeader(req, "X-Auth-Token", c->token,
							SCE_HTTP_HEADER_ADD);
			if (sceHttpSendRequest(req, body, blen) >= 0)
				sceHttpGetStatusCode(req, &status);
			sceHttpDeleteRequest(req);
		}
		sceHttpDeleteConnection(conn);
	}
	sceHttpDeleteTemplate(tpl);

	rc = (status >= 200 && status < 300) ? 0 : -1;
	return rc;
}

/* ---- queue drain ----------------------------------------------------- */

static void append_line(const char *path, const char *line, int len)
{
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (fd < 0) return;
	sceIoWrite(fd, line, len);
	sceIoWrite(fd, "\n", 1);
	sceIoClose(fd);
}

static int file_exists(const char *path)
{
	SceIoStat st;
	return sceIoGetstat(path, &st) >= 0;
}

static void drain_queue(const Config *c)
{
	/* claim a snapshot atomically: kernel keeps appending to a fresh pending */
	if (!file_exists(PT_SENDING)) {
		if (!file_exists(PT_PENDING)) return;
		if (sceIoRename(PT_PENDING, PT_SENDING) < 0) return;
	}

	static char buf[QUEUE_MAX];
	int n = read_file(PT_SENDING, buf, sizeof(buf));
	if (n <= 0) { sceIoRemove(PT_SENDING); return; }

	if (!net_connected()) return;   /* keep the snapshot, retry next pass */

	/* rewrite the snapshot with only the lines we failed to send */
	sceIoRemove(PT_SENDING);
	int failures = 0;

	char *save = NULL;
	for (char *ln = strtok_r(buf, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
		if (ln[0] != '{') continue;

		char title_id[32];
		if (json_str(ln, "titleId", title_id, sizeof(title_id)) < 0) continue;
		long secs = json_num(ln, "seconds");
		if (secs <= 0) continue;

		char title[256];
		resolve_title(title_id, title, sizeof(title));

		if (post_session(c, title_id, title, (uint32_t)secs) < 0) {
			append_line(PT_SENDING, ln, strlen(ln));
			failures++;
		}
	}
	(void)failures;
}

/* ---- worker ---------------------------------------------------------- */

static int worker(SceSize args, void *argp)
{
	(void)args; (void)argp;
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir(PT_DIR, 0777);

	for (;;) {
		Config c;
		if (load_config(&c) == 0) {
			ensure_net();
			drain_queue(&c);
		}
		sceKernelDelayThread((SceUInt)POLL_SEC * 1000 * 1000);
	}
	return sceKernelExitDeleteThread(0);
}

int module_start(SceSize args, void *argp)
{
	(void)args; (void)argp;
	SceUID thid = sceKernelCreateThread("playtime_upload", worker,
					    0x10000100, 0x4000, 0, 0, NULL);
	if (thid >= 0)
		sceKernelStartThread(thid, 0, NULL);
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	(void)args; (void)argp;
	return SCE_KERNEL_STOP_SUCCESS;
}
