/* trophymount.skprx — kernel plugin exposing one syscall, trophyKernelMountById,
 * that mounts a savedata-style PFS folder via SceAppMgr's internal
 * sceAppMgrMountById. The OS reads sce_pfs/ + sce_sys/sealedkey, unseals the
 * per-console key in F00D, and exposes the DECRYPTED folder at a mount point the
 * caller then reads with ordinary sceIo. Trimmed from VitaShell (GPLv3)
 * modules/kernel/main.c — retail 3.65 only. */
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr.h>

#include <string.h>
#include <taihen.h>

#include "../trophymount.h"

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);
int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);

static int _trophyKernelMountById(ShellMountIdArgs *args) {
	int res;

	void *(* sceAppMgrFindProcessInfoByPid)(void *data, SceUID pid);
	int (* sceAppMgrMountById)(SceUID pid, void *info, int id, const char *titleid, const char *path,
	                           const char *desired_mount_point, const void *klicensee, char *mount_point);
	int (* _ksceKernelGetModuleInfo)(SceUID pid, SceUID modid, SceKernelModuleInfo *info);

	tai_module_info_t tai_info;
	tai_info.size = sizeof(tai_module_info_t);
	if (taiGetModuleInfoForKernel(KERNEL_PID, "SceAppMgr", &tai_info) < 0)
		return -1;

	switch (tai_info.module_nid) {
		case 0x1C9879D6: // 3.65 retail
			module_get_offset(KERNEL_PID, tai_info.modid, 0, 0x2DE1, (uintptr_t *)&sceAppMgrFindProcessInfoByPid);
			module_get_offset(KERNEL_PID, tai_info.modid, 0, 0x19E61, (uintptr_t *)&sceAppMgrMountById);
			break;
		default:
			return -2; // unsupported firmware
	}

	res = module_get_export_func(KERNEL_PID, "SceKernelModulemgr",
	                             0xC445FA63, 0xD269F915, (uintptr_t *)&_ksceKernelGetModuleInfo);
	if (res < 0)
		res = module_get_export_func(KERNEL_PID, "SceKernelModulemgr",
		                             0x92C9FFC2, 0xDAA90093, (uintptr_t *)&_ksceKernelGetModuleInfo);
	if (res < 0)
		return res;

	SceKernelModuleInfo mod_info;
	mod_info.size = sizeof(SceKernelModuleInfo);
	res = _ksceKernelGetModuleInfo(KERNEL_PID, tai_info.modid, &mod_info);
	if (res < 0)
		return res;

	uint32_t appmgr_data_addr = (uint32_t)mod_info.segments[1].vaddr;

	SceUID process_id = ksceKernelGetProcessId();

	void *info = sceAppMgrFindProcessInfoByPid((void *)(appmgr_data_addr + 0x500), process_id);
	if (!info)
		return -1;

	char process_titleid[12];
	char path[256];
	char desired_mount_point[16];
	char mount_point[16];
	char klicensee[16];

	memset(mount_point, 0, sizeof(mount_point));

	if (args->process_titleid)
		ksceKernelStrncpyUserToKernel(process_titleid, args->process_titleid, 11);
	if (args->path)
		ksceKernelStrncpyUserToKernel(path, args->path, 255);
	if (args->desired_mount_point)
		ksceKernelStrncpyUserToKernel(desired_mount_point, args->desired_mount_point, 15);
	if (args->klicensee)
		ksceKernelMemcpyUserToKernel(klicensee, args->klicensee, 0x10);

	res = sceAppMgrMountById(process_id,
	                         (void *)((char *)info + 0x580),
	                         args->id,
	                         args->process_titleid ? process_titleid : NULL,
	                         args->path ? path : NULL,
	                         args->desired_mount_point ? desired_mount_point : NULL,
	                         args->klicensee ? klicensee : NULL,
	                         mount_point);

	if (args->mount_point)
		ksceKernelStrncpyKernelToUser(args->mount_point, mount_point, 15);

	return res;
}

int trophyKernelMountById(ShellMountIdArgs *args) {
	uint32_t state;
	ENTER_SYSCALL(state);

	ShellMountIdArgs k_args;
	ksceKernelMemcpyUserToKernel(&k_args, args, sizeof(ShellMountIdArgs));

	int res = ksceKernelRunWithStack(0x2000, (void *)_trophyKernelMountById, &k_args);

	EXIT_SYSCALL(state);
	return res;
}

void _start() __attribute__ ((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
	return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize args, void *argp) {
	return SCE_KERNEL_STOP_SUCCESS;
}
