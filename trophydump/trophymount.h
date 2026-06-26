/* Shared between the trophydump kernel plugin and the user app.
 * ShellMountIdArgs + the kernel syscall that mounts a savedata-style PFS folder
 * by SceAppMgr mount-id (the OS unseals the per-console sealedkey via F00D and
 * exposes the DECRYPTED contents at a normal mount point). Layout/behaviour
 * copied from VitaShell (GPLv3) modules/kernel. */
#ifndef TROPHYMOUNT_H
#define TROPHYMOUNT_H

#include <stdint.h>

typedef struct {
	int id;                          /* SceAppMgr mount id (0x12F = ur0:user/00/trophy/data) */
	const char *process_titleid;     /* our app's title id */
	const char *path;                /* folder to mount, e.g. ur0:user/00/trophy/data/NPWR..._00/ */
	const char *desired_mount_point; /* NULL = auto */
	const void *klicensee;           /* 16 bytes; ZERO for trophy/savedata (key comes from sealedkey) */
	char *mount_point;               /* OUT: plaintext mount point (e.g. "uma0:") */
} ShellMountIdArgs;

/* Kernel syscall exported by trophymount.skprx (see kernel/exports.yml). */
int trophyKernelMountById(ShellMountIdArgs *args);

#endif
