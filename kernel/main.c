/*
 * VitaPlaytimeTracker — kernel module (playtime_k.skprx)
 *
 * Reliable on-console session detection for HENkaku/taiHEN PS Vita. Hooks the
 * process-event handler and times each foreground game, then appends finished
 * sessions to a local queue file (pending.jsonl). It does NOT touch the network —
 * the companion user module (playtime_u.suprx) drains the queue and uploads.
 *
 * Detection (the event hook, the SceProcessmgr NID, the system-app filter) is
 * adapted from BetterTrackPlug by FMudanyali (GPLv3); the session model
 * (start tick -> on stop emit one {titleId,seconds} record) is ours.
 *
 * Events from ksceKernelInvokeProcEventHandler:  1=Startup 5=Resume 3=Exit 4=Suspend
 *
 * License: GPLv3.
 */
#include <stdio.h>
#include <string.h>
#include <taihen.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/processmgr.h>
#include <psp2kern/kernel/sysroot.h>   /* ksceKernelGetProcessTitleId -> ksceKernelSysrootGetProcessTitleId */
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>

#define SECOND 1000000ULL

#define PT_DIR   "ux0:data/VitaPlaytime"
#define PT_QUEUE PT_DIR "/pending.jsonl"

static tai_hook_ref_t event_handler_ref;
static SceUID hooks[1];

/* one open session held in memory */
static char     cur_tid[32];
static uint64_t cur_start;          /* system tick (us) at session start */
static int      have;               /* a session is open */

/* Skip the shell and system apps: title ids "main" (SceShell) and "NPXS...". */
static int is_system_title(const char *tid)
{
	return (tid[0] == 0) ||
	       (strncmp(tid, "main", 4) == 0) ||
	       (strncmp(tid, "NPXS", 4) == 0);
}

/* Append one finished session as a JSON line to the queue.
 * Kept to %s/%u only — kernel snprintf (SceSysclib) can't be trusted with %llu. */
static void emit_session(const char *tid, uint32_t secs, uint32_t ended)
{
	if (secs == 0) return;

	char line[160];
	int n = snprintf(line, sizeof(line),
		"{\"titleId\":\"%s\",\"seconds\":%u,\"endedAt\":%u}\n", tid, secs, ended);
	if (n <= 0) return;

	SceUID fd = ksceIoOpen(PT_QUEUE,
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (fd < 0) return;
	ksceIoWrite(fd, line, n);
	ksceIoClose(fd);
}

static void session_start(const char *tid)
{
	/* already timing this exact title? keep the original start */
	if (have && strncmp(cur_tid, tid, sizeof(cur_tid) - 1) == 0)
		return;

	strncpy(cur_tid, tid, sizeof(cur_tid) - 1);
	cur_tid[sizeof(cur_tid) - 1] = 0;
	cur_start = ksceKernelGetSystemTimeWide();
	have = 1;
}

static void session_stop(void)
{
	if (!have) return;
	uint64_t now = ksceKernelGetSystemTimeWide();
	uint32_t secs = (uint32_t)((now - cur_start) / SECOND);
	uint32_t ended = (uint32_t)ksceKernelLibcTime(0);  /* Unix seconds at session end */
	emit_session(cur_tid, secs, ended);
	have = 0;
}

/* Process lifecycle handler (hooked). */
static int event_handler(int pid, int ev, int a3, int a4, int *a5, int a6)
{
	char tid[32];
	ksceKernelGetProcessTitleId(pid, tid, sizeof(tid));

	if (!is_system_title(tid)) {
		switch (ev) {
		case 1: /* Startup */
		case 5: /* Resume  */
			session_start(tid);
			break;
		case 3: /* Exit    */
		case 4: /* Suspend */
			/* only close if this stop belongs to the open session */
			if (have && strncmp(cur_tid, tid, sizeof(cur_tid) - 1) == 0)
				session_stop();
			break;
		}
	}

	return TAI_CONTINUE(int, event_handler_ref, pid, ev, a3, a4, a5, a6);
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp)
{
	ksceIoMkdir("ux0:data", 0777);
	ksceIoMkdir(PT_DIR, 0777);

	hooks[0] = taiHookFunctionImportForKernel(KERNEL_PID,
		&event_handler_ref,
		"SceProcessmgr",
		TAI_ANY_LIBRARY,
		0x414CC813,           /* ksceKernelInvokeProcEventHandler */
		event_handler);

	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
	/* close whatever was open so the last session isn't lost on unload */
	session_stop();
	if (hooks[0] >= 0)
		taiHookReleaseForKernel(hooks[0], event_handler_ref);
	return SCE_KERNEL_STOP_SUCCESS;
}
