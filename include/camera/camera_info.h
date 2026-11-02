// SPDX-License-Identifier: MIT
/** @file camera_info.h
 *  @brief 摄像头能力表（指纹 + 格式/分辨率/帧率清单）的采集与持久化
 *
 *  【它负责什么】
 *  ① 从已打开的设备读指纹（card / bus / serial）
 *  ② 枚举设备支持的全部「格式 × 尺寸 × 帧率」
 *  ③ 把指纹 + 清单写进 camera.ini，下次启动比对指纹决定要不要重新枚举
 *  【它不负责什么】
 *  · 不碰 Qt（整个 capture/ 目录零 Qt 依赖，这条线不能破）
 *  · 不下发 S_FMT / S_PARM（那是 capture_set_format 的事）
 *  · 不做业务决策（用哪个分辨率是界面和配置的事）
 *  【为什么 serial 要绕一圈】
 *  struct v4l2_capability 里**没有** serial 字段（5.4 的 UAPI 就是这样，
 *  只有 driver / card / bus_info / version / capabilities）。
 *  serial 挂在 media 设备的 struct media_device_info 上，
 *  所以要用 bus_info 去 /dev/mediaN 里匹配出对应的那个 media 节点再问一次。
 *  【谁调用这一层】
 *  capture.c 的 capture_init()  → camera_sync_caps()（写）
 *  Qt 界面（设置页/摄像头页）   → cam_ini_load()（只读）
 *  测试程序 tests/             → 全是公开函数，可单独调
 */
#ifndef CAMERA_INFO_H
#define CAMERA_INFO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* camera.ini 的位置。
 * 用绝对路径而不是相对于可执行文件：程序正常由 deploy.sh 从 /root 拉起，
 * 但也可能被手工或服务脚本拉起来，CWD 不保证是 /root。
 * ★ /etc/dvr 在 rootfs 上（ext4 可写分区），掉电不会丢。 */
#define CAMERA_INI_PATH   "/etc/dvr/camera.ini"

/* 最多记多少条「格式×尺寸×帧率」。这颗摄像头 11 条，
 * 64 是留余量：换个支持 YUYV 各档的摄像头会多不少。 */
#define CAM_MAX_MODES     64

/* 指纹字段容量。40 字节够装 "usb-sunxi-ehci-1.2"（18）和
 * UVC 的 serial（通常 8~16 位）。超出会被 snprintf 截断，不影响使用。 */
#define CAM_FP_LEN        40

/** @brief 一条采集能力：格式 × 尺寸 × 帧率。 */
typedef struct {
    char     fourcc[5];   ///< "MJPG" / "YUYV"，4 字符 + '\0'
    unsigned width;       ///< 宽（像素）
    unsigned height;      ///< 高（像素）
    unsigned fps;         ///< **0 = 驱动对这个尺寸没报帧率**，不是 0fps
} CamMode;

/** @brief 设备指纹：用来判断"是不是同一颗摄像头"。 */
typedef struct {
    char card[CAM_FP_LEN];      ///< VIDIOC_QUERYCAP 的 card，如 "Web Camera: Web Camera"
    char bus[CAM_FP_LEN];       ///< bus_info，如 "usb-sunxi-ehci-1.2"（插在哪个口）
    char serial[CAM_FP_LEN];    ///< media 设备报的 serial；可能为空
    char queried[24];           ///< "YYYY-MM-DD HH:MM:SS"，写文件的时刻
} CamFingerprint;

/** @brief 从已打开的 fd 读设备指纹。
 *
 *  @param fd 已打开的设备 fd。
 *  @param fp 输出指纹。
 *  @return 0 成功；-1 失败。
 *
 *  @note serial 拿不到就留空，**不算失败** —— 部分 UVC 不报 serial。
 *  @note serial 要绕一圈：v4l2_capability 里没有这个字段，得用 bus_info
 *        去 /dev/mediaN 里匹配出对应的 media 节点再问一次。
 */
int cam_fp_read(int fd, CamFingerprint *fp);

/** @brief 比对两个指纹是否相同。
 *
 *  @param a       旧指纹。
 *  @param b       新指纹。
 *  @param diff    输出人类可读差异（如 "serial 20260815 → 20260901"）；可为 NULL。
 *  @param diffLen diff 缓冲区长度。
 *  @return 1 = 完全相同；0 = 有差异。
 *
 *  @warning **参数顺序决定箭头方向** —— cam_fp_same(&旧, &新, ...) 才得到 "旧 → 新"。
 */
int cam_fp_same(const CamFingerprint *a, const CamFingerprint *b,
                char *diff, size_t diffLen);

/** @brief 枚举设备支持的全部「格式 × 尺寸 × 帧率」。
 *
 *  @param fd         已打开的设备 fd。
 *  @param onlyFourcc 只枚举该格式（如 "MJPG"）；NULL = 所有格式。
 *  @param out        输出数组，**调用方分配**。
 *  @param max        out 容量（条目数）。
 *  @return 实际写入条数（可能为 0）；负数表示出错。
 *
 *  @note 只展开 DISCRETE 尺寸；STEPWISE / CONTINUOUS 无法穷举，只打一行日志跳过。
 *  @note 耗时：这颗摄像头约 40~80 次 ioctl、十几毫秒 —— 只在"指纹变了/没缓存"时调。
 */
int cam_enum_modes(int fd, const char *onlyFourcc, CamMode *out, int max);

/** @brief 读 camera.ini 里的能力表。
 *
 *  @param path  文件路径，通常 @ref CAMERA_INI_PATH。
 *  @param fp    输出指纹；可为 NULL。
 *  @param modes 输出模式数组；**NULL 表示只数条数**（命中缓存时省一次大数组拷贝）。
 *  @param max   modes 容量。
 *  @return 文件里有效的模式条数（0 = 文件不存在或没内容）；-1 出错。
 *
 *  @warning modes 非 NULL 但文件里条数 > @p max 时，超出部分**不写入**，
 *           但返回值**仍照实返回总条数** —— 调用方要用 min(返回值, max) 作有效条数。
 */
int cam_ini_load(const char *path, CamFingerprint *fp, CamMode *modes, int max);

/** @brief 原子写 camera.ini（临时文件 → fsync → rename）。
 *
 *  @param path  目标路径。
 *  @param fp    指纹；其 queried 字段为空时**自动填当前时间**。
 *  @param modes 模式数组。
 *  @param count 条数。
 *  @return 0 成功；-1 失败。
 *
 *  @note 为什么要原子写：直接 "w" 打开会在开头就清空旧文件，此刻掉电 = 文件变 0 字节，
 *        下次启动能力表丢失。tmp + rename 最坏只丢这一次更新。
 */
int cam_ini_save(const char *path, const CamFingerprint *fp,
                 const CamMode *modes, int count);

/** @brief 同步能力表：读指纹 → 比对 → 命中缓存跳过枚举，否则枚举并覆盖 camera.ini。
 *
 *  @param fd      已打开的设备 fd。
 *  @param devPath 设备路径（读 serial 时用于定位 media 节点）。
 *  @return 本次可用的模式条数；-1 出错。
 *
 *  @note 由 capture_init() 调用 —— 这是"第一次见到这颗摄像头就枚举一次"的地方。
 *  @note 为什么它自己读指纹而不接受参数：调用点只有 capture_init 一处，
 *        少一个参数少一次"传错设备"的机会。
 */
int camera_sync_caps(int fd, const char *devPath);

#ifdef __cplusplus
}
#endif


/* =============================================================================
 *  【并发约定】
 *
 *    写方：采集线程   capture_init() → camera_sync_caps() → cam_ini_save()
 *    读方：UI 线程    CameraService::modeLoad() → cam_ini_load()
 *
 *    两方**没有加锁**，不加锁的依据：
 *      · cam_ini_save 是「tmp 文件 → fsync → rename」的原子替换，
 *        读方要么读到旧全文、要么读到新全文，不会读到半截；
 *      · 本模块**没有内存态**（无 static 全局），每次都直接读写文件，无共享可变数据。
 *
 *    已知且可接受的不一致：换摄像头重枚举的瞬间，UI 若恰好读了旧表，
 *    显示的是上一颗的模式 —— 重新进页即刷新。不做更强的同步，
 *    因为能力表运行期几乎不变（只有插拔不同摄像头才写）。
 * ========================================================================== */

#endif /* CAMERA_INFO_H */
