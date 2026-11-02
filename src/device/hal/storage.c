// SPDX-License-Identifier: MIT
/** @file storage.c
 *  @brief 存储卡挂载判断与容量查询
 */
#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

/* =============================================================================
 *  判断某个路径是不是【真的挂载点】
 * =============================================================================
 *  /proc/mounts 每行格式：设备 挂载点 类型 选项 0 0
 *  只取前三列就够；第四列（选项）里可能有空格转义（\040），所以必须逐个 %s 读，
 *  不能用 fgets + strtok。
 *
 *  【为什么不能只看设备名】
 *  判 "mmcblk1p1 在 /proc/mounts 里" 也不行 —— 它可能被挂到别的地方
 *  （比如旧脚本挂在 /mnt/sdcard/mmcblk1p1）。必须比挂载点。
 * ========================================================================== */
int storage_mounted(const char *mountpoint)
{
    if (!mountpoint || !mountpoint[0])
        return 0;

    FILE *f = fopen("/proc/mounts", "r");
    if (!f)
        return 0;

    char dev[256], mnt[256], type[64];
    int found = 0;

    /* fscanf 返回成功读到的字段数；读到 3 个才算一行完整 */
    while (fscanf(f, "%255s %255s %63s", dev, mnt, type) == 3) {
        if (strcmp(mnt, mountpoint) == 0) {
            found = 1;
            break;
        }
    }

    fclose(f);
    return found;
}

int storage_capacity(const char *mountpoint,
                     unsigned long long *totalKb,
                     unsigned long long *availKb)
{
    /* 先确认真的挂着 —— 否则 statvfs 会返回 rootfs 的容量，
     * 调用方会以为"卡还有 500M 可用"，其实是 eMMC。 */
    if (!storage_mounted(mountpoint))
        return -1;

    struct statvfs vfs;
    if (statvfs(mountpoint, &vfs) != 0)
        return -1;

    if (totalKb)
        *totalKb = (unsigned long long)vfs.f_blocks * vfs.f_frsize / 1024ULL;
    if (availKb)
        *availKb = (unsigned long long)vfs.f_bavail * vfs.f_frsize / 1024ULL;

    return 0;
}
