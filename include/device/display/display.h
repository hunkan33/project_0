// SPDX-License-Identifier: MIT
/** @file display.h
 *  @brief DE 视频图层的对外接口（零拷贝直出 NV12）
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "typedef.h"
#include "sunxi_display2_k54.h"

/* =============================================================================
 *  视频显示布局（纯参数打包，本身不定义任何预设）
 * =============================================================================
 *  DE 图层不做等比缩放，只做「从源图裁一块 → 铺到屏幕一块」的直接映射。
 *  所以 cropW/cropH 和 winW/winH 必须同比例，否则画面会被拉伸变形。
 *
 *  ★ 预设布局不在这里定义 —— 唯一真相源是 page.h 的 pageLayout()。
 *    每一页显示什么区域，由 PageLayout 描述；AppController 负责把它
 *    转成本结构体再调 display_set_layout()。
 *    这样"某页显示在哪"只有一处定义，不会出现两处不同步。
 * ========================================================================== */

/** @brief 一套视频显示布局：从源图裁哪块 → 铺到屏幕哪块。
 *
 *  @note DE 图层**不做等比缩放**，只做直接映射，所以 crop 与 win 必须同比例，
 *        否则画面被拉伸变形。
 *  @note 预设布局不在这里 —— 唯一真相源是 page.h 的 pageLayout()。
 */
typedef struct DisplayLayout {
    int cropX, cropY, cropW, cropH;   ///< 从源图（如 1280×720）裁哪一块
    int winX,  winY,  winW,  winH;    ///< 铺到屏幕（1024×600）哪一块
} DisplayLayout;

/** @brief 初始化 DE 视频图层（程序启动时调一次）。
 *
 *  @param srcW       解码输出的源宽（如 1280）。
 *  @param srcH       解码输出的源高（如 720）。
 *  @param cropY      初始裁剪上偏移；会被 display_set_layout() 覆盖。
 *  @param cropH      初始裁剪高度；同上。
 *  @param winX       初始显示区左上角 x；同上。
 *  @param winY       初始显示区左上角 y；同上。
 *  @param winW       初始显示区宽；同上。
 *  @param winH       初始显示区高；同上。
 *  @return 0 成功；-1 失败。
 *
 *  @note 内部会先把 fb0 的视频区清 0（让 ch[0] 透出来），但**不清顶栏底栏**。
 *  @warning 本函数在采集线程里跑，和 AppController 的 250ms 定时器没有先后保证，
 *           所以它**不清 g_visible**（否则会把"要显示视频"的意图擦掉）。
 */
int  display_init(int srcW, int srcH, int cropY, int cropH,
                  int winX, int winY, int winW, int winH);

/** @brief 把一帧送进 DE 图层（每帧调）。
 *
 *  @param fd @ref decoder_y_fd 拿到的 dma_buf fd。
 *  @return 0 成功；-1 失败。
 *
 *  @note 内部有 g_visible 门控：视频层被 display_set_visible(0) 关掉时，
 *        这里**只缓存 fd、不下发 ioctl**。所以采集线程不需要知道当前在哪个页面，
 *        照常每帧调用即可（关层期间调用是零成本的）。
 */
int  display_update_fd(int fd);

/** @brief 开关视频层（ch[0]）—— 页面切换时控制"视频区该不该透出来"。
 *
 *  @param on 1 = 显示（若已收到过帧，**立即**用最近那帧打开，不等下一帧）；
 *            0 = 隐藏（关图层，并停止后续每帧的 ioctl 下发）。
 *  @return 0 成功；-1 失败。
 *
 *  @note 只给 AppController 调（写接口）；capture_thread 用 display_is_visible() 读。
 *  @note 销毁解码器前**必须先** set_visible(0) —— 否则 DE 还在扫那块已释放的内存。
 */
int  display_set_visible(int on);

/** @brief 查询视频层当前是否显示。
 *
 *  @return 1 = 显示中；0 = 已关闭。
 *
 *  @note 这是**读**接口，给 capture_thread 用；写接口是 display_set_visible()。
 *
 *  @note **已接上（2026-10-31）**：capture_thread.cpp 主循环里，
 *        `capture_grab` 拿到帧之后、`decoder_decode()` 之前有一句
 *        `if (!display_is_visible()) { capture_release(fd); continue; }` ——
 *        视频层关着时（设置页 / 摄像头页）整条 VE 解码流水线都不跑。
 *  @warning 那边**必须先 `capture_release` 再 `continue`**。4 块缓冲靠 QBUF
 *           循环复用，漏还一次少一块，4 次后队列空 → poll 永远超时 →
 *           被主循环判成"设备异常"去重连（表现为"设置页待一会儿，切回来黑屏重连"）。
 *  @note 实测收益：设置页期间 CPU 从 2% 降到 0%（单核）。**不是 41%** ——
 *        13.7ms/帧是解码延迟（含等 VE 硬件），真正 CPU 只有约 0.67ms/帧。
 *        数据见 `18-视频层门控与CPU实测.md`。
 */
int  display_is_visible(void);

/** @brief 应用一套显示布局（改 crop + screen_win，立即下发）。
 *
 *  @param layout 目标布局；内部会拷一份，调用方不必保持生命周期。
 *  @return 0 成功；-1 失败。
 *
 *  @note 在 set_visible(1) 之前或之后调都可以（都立即生效）。
 *        **推荐顺序**：先 set_layout 再 set_visible(1) —— 这样图层打开的那一刻
 *        就是正确布局，不会先闪一帧旧布局。
 */
int  display_set_layout(const DisplayLayout *layout);

/** @brief 查询当前解码输出的源宽（= display_init() 的 srcW）。
 *
 *  @return 源宽；**0 表示采集线程还没完成格式协商**（display_init 还没跑）。
 *
 *  @note 给 AppController 用：page.cpp 里的 crop 按 PAGE_REF_W×PAGE_REF_H 写死，
 *        实际分辨率变了得按这里返回的值等比换算。
 */
int  display_src_w(void);

/** @brief 查询当前解码输出的源高（= display_init() 的 srcH）。
 *
 *  @return 源高；0 表示还没协商完成。
 */
int  display_src_h(void);

/** @brief 换分辨率时更新源尺寸（不销毁图层，只改 size[]）。
 *
 *  @param srcW 新的源宽。
 *  @param srcH 新的源高。
 *  @return 0 成功；-1 失败。
 *
 *  @note 为什么不直接 destroy + init：destroy 会 close(/dev/disp) 并清 g_visible，
 *        视频层被关掉还得等 AppController 重新打开；而这里只需要把 NV12 的
 *        宽高换掉，图层保持原状。
 *  @note crop 不在这里改 —— 由 AppController 按新尺寸重算后调 display_set_layout()。
 */
int  display_set_src_size(int srcW, int srcH);

/** @brief 销毁 DE 图层（程序退出时调一次）。
 *
 *  @note 会 close(/dev/disp) 并清 g_visible。
 *  @warning 必须在**解码器销毁之后**才调（先关层、再销毁解码器、最后销毁图层），
 *           顺序反了 DE 会扫到已释放的内存。
 */
void display_destroy(void);

#endif /* DISPLAY_H */
