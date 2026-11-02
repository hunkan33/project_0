// SPDX-License-Identifier: MIT
/** @file capture.h
 *  @brief V4L2 采集层的对外接口
 *
 *  这是 capture.c 的头文件，也是上层（capture_thread.cpp）唯一需要包含的头。
 *  【典型调用顺序】—— 顺序错了会 EBUSY / 死锁，照抄即可：
 *  int fd = capture_init();                  // ① 找设备 + 打开
 *  capture_set_format(fd, w, h, fps);        // ② 提格式要求（必须在开流前）
 *  capture_query_format(fd, &aw, &ah, &af);  // ③ 读回实际生效值
 *  ...decoder_init(aw, ah); display_init(...);// ④ 按实际尺寸建下游
 *  capture_start(fd);                        // ⑤ 分配缓冲 + 开流
 *  ── 循环 ──
 *  capture_grab(fd);                         // ⑥ 取一帧（返回 0/-2/-1）
 *  capture_frame_data(); capture_frame_size();
 *  capture_release(fd);                      // ⑦ 必须还回去
 *  ── 结束 ──
 *  capture_stop(fd);                         // ⑧ 停流 + 释放
 *  close(fd);
 *  【换格式】不能直接调 capture_set_format —— 流跑着的时候会 EBUSY。
 *  正确做法：capture_stop() → set_format → start()，见 capture_thread.cpp 的
 *  switch_format()；或者从 UI 侧调 capture_request_format() 让采集线程去做。
 */
#ifndef CAPTURE_H
#define CAPTURE_H

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>

/* ---- 采集：默认请求值（兜底用，不是"实际采集尺寸"）----
 * 这两个只在【配置里没写】或【协商失败】时起作用。
 * 实际采集分辨率是运行时和摄像头协商出来的，拿它请用 capture_query_format()。 */
/** @brief 默认请求宽度（像素）。只在配置缺失或协商失败时兜底。 */
#define CAP_W    1280
/** @brief 默认请求高度（像素）。同上。 */
#define CAP_H    720
/** @brief 采集格式，固定 MJPEG（这颗摄像头 UVC 只支持 MJPG / YUYV）。 */
#define CAP_FMT  V4L2_PIX_FMT_MJPEG

/* ---- 初始裁剪（只给 display_init 一个合法初值）----
 * 真正的 crop 由 AppController::toDisplayLayout() 按实际采集分辨率和窗口比例算，
 * 每次切页/换分辨率都会重新下发，覆盖这里的值。
 *
 * 【为什么只有 Y/H 两个】
 *   X 恒为 0（取满宽）、W 恒等于源宽 —— 这两条是 toDisplayLayout() 里的硬规则，
 *   不是"可配置参数"，所以不保留 CROP_X / CROP_W，免得有人以为可以改。 */
/** @brief 裁剪区上偏移（720p 基准值，实际由 toDisplayLayout() 覆盖）。 */
#define CROP_Y   60
/** @brief 裁剪区高度（720p 基准值，同上）。 */
#define CROP_H   600

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 扫描 /dev/video0..63，找到第一个能采集的 UVC 设备并打开。
 *
 *  @return 成功返回 fd（一直保持，交给采集线程用）；没找到返回 -1。
 *
 *  @note 找到设备时会顺带调 camera_sync_caps() 同步能力表（写 camera.ini）——
 *        这是"第一次见到这颗摄像头就枚举一次"的地方。
 *  @note 只能在采集线程初始化时调一次。
 */
int capture_init(void);

/** @brief 请求采集格式（分辨率 + 帧率；pixelformat 固定 MJPEG）。
 *
 *  @param fd  由 capture_init() 返回的设备 fd。
 *  @param w   期望宽度（像素）。
 *  @param h   期望高度（像素）。
 *  @param fps 期望帧率；0 表示不改帧率。
 *  @return 0 = S_FMT 调用成功；-1 = S_FMT 直接失败。
 *
 *  @warning **返回 0 不代表请求的分辨率被接受。** S_FMT 是协商不是命令，
 *           驱动可能给你另一个尺寸。必须用 capture_query_format() 读回确认。
 *  @note 必须在 capture_start() 之前调 —— 开流后再 S_FMT 会 EBUSY。
 *  @note 帧率走 VIDIOC_S_PARM 的 timeperframe（S_FMT 不管帧率）；
 *        UVC 对帧率支持很有限，设不了也不报错。
 */
int capture_set_format(int fd, unsigned w, unsigned h, unsigned fps);

/** @brief 读回设备当前实际生效的格式。
 *
 *  @param fd  设备 fd。
 *  @param w   输出宽度；可为 NULL。
 *  @param h   输出高度；可为 NULL。
 *  @param fps 输出帧率（由 timeperframe 换算）；可为 NULL。
 *  @return 0 成功；-1 失败。
 *
 *  @note @p fps 返回 0 表示**驱动没有上报帧率**，不是"0 帧"。
 */
int capture_query_format(int fd, unsigned *w, unsigned *h, unsigned *fps);

/** @brief 登记一个换格式请求（UI 线程 → 采集线程）。
 *
 *  @param w   目标宽；必须非 0。
 *  @param h   目标高；必须非 0。
 *  @param fps 目标帧率；0 表示不改帧率。
 *  @return 0 = 请求已登记（**并非已生效**）。
 *
 *  @note 为什么不让 UI 直接调 capture_set_format：
 *        采集线程拥有 video_fd / 解码器 / DE 句柄，S_FMT 会打断正在跑的流，
 *        跨线程做会和 DQBUF 打架。所以只能"登记请求"，由采集线程在主循环里自己执行。
 *  @note 线程安全：内部有 mutex，只保护三个 unsigned，UI 线程随便调。
 */
int capture_request_format(unsigned w, unsigned h, unsigned fps);

/** @brief 取走挂起的换格式请求。
 *
 *  @param w   输出目标宽；可为 NULL。
 *  @param h   输出目标高；可为 NULL。
 *  @param fps 输出目标帧率；可为 NULL。
 *  @return 1 = 有请求并已写出参数；0 = 没有挂起的请求。
 *
 *  @warning **取走即清标志** —— 调用方必须真的执行这次换格式，
 *           否则请求就永久丢失了（界面上会表现为"点了保存但没换"）。
 */
int capture_take_format_request(unsigned *w, unsigned *h, unsigned *fps);

/** @brief 开流：REQBUFS(4) → QUERYBUF+mmap → QBUF×4 → STREAMON。
 *
 *  @param fd 设备 fd。
 *  @return 0 成功；-1 失败（每一步单独 perror，日志里能看到断在哪）。
 *
 *  @note 前置条件：格式已经设好（先调 capture_set_format）。
 *  @warning mmap 表是**静态全局**的，重复 start 而不 stop 会泄漏映射。
 */
int capture_start(int fd);

/** @brief 取一帧（出队）。
 *
 *  @param fd 设备 fd。
 *  @return 三态，调用方必须分开处理：
 *          - @c 0  拿到一帧 → 用 capture_frame_data()/size() 取，用完**必须** capture_release()
 *          - @c -2 本轮没帧（poll 超时 / 被信号打断）→ **不是错误**，继续下一轮
 *          - @c -1 真出错（设备掉线 / DQBUF 失败）→ 该走重连
 *
 *  @note 把 -2 和 -1 混为一谈会导致 USB 带宽抖动时误重连，采集频繁重启。
 */
int capture_grab(int fd);

/** @brief 当前帧（capture_grab 返回 0 之后有效）的起始地址。
 *
 *  @return 指向 mmap 进来的驱动内存的指针。
 *
 *  @warning 指向驱动内存，**调用期间不能 release**，否则这块 buffer 可能被
 *           驱动重新填新帧，你看到的是撕裂画面。
 */
void  *capture_frame_data(void);

/** @brief 当前帧的字节数。
 *
 *  @return MJPEG **压缩后**大小，不是 w×h×bpp。
 */
size_t capture_frame_size(void);

/** @brief 把当前帧还给驱动（VIDIOC_QBUF）。
 *
 *  @param fd 设备 fd。
 *
 *  @note 没借出时是空操作，可安全重复调用。
 *  @warning 不还的后果：4 块 buffer 用完后队列空，poll 永远超时 → **画面卡死**。
 */
void   capture_release(int fd);

/** @brief 停止采集并释放资源（STREAMOFF + munmap + REQBUFS(0)）。
 *
 *  @param fd 设备 fd。
 *
 *  @warning 换分辨率前**必须先调它** —— V4L2 规定 buffer 已分配时
 *           S_FMT 会返回 EBUSY。只 munmap 不够，必须 REQBUFS(0)。
 */
void   capture_stop(int fd);

#ifdef __cplusplus

int capture_negotiate(int fd, unsigned *outW, unsigned *outH, unsigned *outFps);

}
#endif

/* =============================================================================
 *  启动时的视频窗（"开层给 DE"的初始参数，归采集域；AppController 之后按页覆盖）
 *
 *  屏幕是 1024 × 600，布局如下：
 *
 *      y = 0    ┌──────────────────┐
 *               │  顶栏 48px        │  ← Qt 画
 *      y = 48   ├──────────────────┤  ← WIN_Y
 *               │  视频 480px       │  ← WIN_H
 *      y = 528  ├──────────────────┤
 *               │  底栏 72px        │  ← Qt 画
 *      y = 600  └──────────────────┘
 *
 *   核对：48 + 480 + 72 = 600。win 是屏幕坐标，与采集分辨率无关。
 * ========================================================================== */
#define WIN_X       0       /* 视频区左上角 x（贴左边缘）*/
#define WIN_Y       48      /* 视频区左上角 y（= 顶栏高度）*/
#define WIN_W       1024    /* 视频区宽度（= 屏幕宽）*/
#define WIN_H       480     /* 视频区高度（= 600 - 48 顶栏 - 72 底栏）*/


#endif
