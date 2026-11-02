// SPDX-License-Identifier: MIT
/** @file storage.h
 *  @brief 存储卡（SD）的挂载判断与容量查询
 *
 *  【为什么单独一个模块】
 *  "卡在不在"这个判断有两个地方要用：顶栏显示、录制落盘。
 *  写两份就会变成两套判据 —— 一处用 access() 一处用 /proc/mounts，
 *  然后其中一个在拔卡后静默写到了 eMMC 上。所以只留这一个实现。
 *  【为什么判据是 /proc/mounts 而不是 access()/statvfs()】
 *  卡拔掉以后 /mnt/sdcard 这个**目录还在**（它挂在 rootfs 上，是个普通空目录），
 *  于是：
 *  · access("/mnt/sdcard", W_OK) 照样返回可写
 *  · statvfs 拿到的是 rootfs 的容量，看起来"还有空间"
 *  两者都会让程序以为卡还在，录像就写到 eMMC 里去了 —— 插回卡也看不到。
 *  只有 /proc/mounts 第二列是内核视角的挂载点，卡一拔这行立刻消失。
 */
#ifndef STORAGE_H
#define STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 存储卡的挂载点。
 *
 *  @warning 改这里必须**同时**改 /etc/mdev/sdcard_insert 和 sdcard_remove，
 *           否则程序认的路径和内核实际挂载的路径对不上。
 *  @note 2026-10-31 从 /mnt/sdcard/<设备名> 改成 /mnt/sdcard。
 */
#define STORAGE_ROOT "/mnt/sdcard"

/** @brief 判断某个路径是不是**真的挂载点**。
 *
 *  @param mountpoint 要检查的绝对路径，通常是 @ref STORAGE_ROOT。
 *  @return 1 = 已挂载（卡在）；0 = 没挂载（没卡 / 挂载失败）。
 *
 *  @warning 不要用 access() 或 statvfs() 替代它。卡拔掉后挂载点目录仍然存在
 *           （挂在 rootfs 上是个普通空目录），access 照样返回可写，
 *           statvfs 拿到的是 eMMC 的容量 —— 程序会以为卡还在，静默写到 eMMC 里。
 *  @note 实现是查 /proc/mounts 第二列，卡拔出后内核会立刻删掉那一行。
 */
int storage_mounted(const char *mountpoint);

/** @brief 读存储卡容量。
 *
 *  @param mountpoint 挂载点，通常是 @ref STORAGE_ROOT。
 *  @param totalKb    输出总容量（KB）；可为 NULL。
 *  @param availKb    输出当前可写容量（KB）；可为 NULL。
 *  @return 0 成功；-1 失败（没挂载 / statvfs 出错）。
 *
 *  @note 失败时**不改动**输出参数（调用方可以先填哨兵值判断）。
 *  @note 内部用 f_bavail 而不是 f_bfree：f_bfree 含 root 保留块，
 *        对普通用户不可用；f_bavail 才是"当前真能写多少"。
 *  @note 内部先调 storage_mounted() 挡掉"没挂载"的情况，避免拿到 rootfs 的容量。
 */
int storage_capacity(const char *mountpoint,
                     unsigned long long *totalKb,
                     unsigned long long *availKb);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
