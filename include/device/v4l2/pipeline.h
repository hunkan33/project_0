// SPDX-License-Identifier: MIT
/** @file pipeline.h
 *  @brief 出图管线：把「解码器 + 显示层」当成一个整体来开、喂、关
 *
 *  【它解决什么问题】
 *    在这之前，`capture_thread.cpp` 里散着 decoder / display 的 init / destroy /
 *    set_visible / set_src_size —— 一共 4 处调用点，全靠"记得按顺序写"。
 *    而 `decoder.h` 定了一条契约：
 *
 *        **`decoder_destroy()` 之前必须先 `display_set_visible(0)`**
 *
 *    （DE 图层此刻还引用着被 free 的 dma_buf fd，不先关层会让 DE 在下一个 vsync
 *      dma_buf_get 失败。）
 *
 *    结果这条契约在 `switch_format()` 里兑现了，在**退出清理里漏了** ——
 *    实测：进程退出后 DE `ch[0]` 仍然 enable、`fb[1280,720]`，指着已释放的 ION 缓冲，
 *    屏幕最后一帧冻结。（20 号文 C1）
 *
 *    把三个动作收进 `pipeline_close()` 之后，**契约只剩一个出口**，漏不掉。
 *
 *  【用它的规矩】
 *    · 开（**只在程序启动时**）：`pipeline_open()` —— decoder_init + display_init
 *    · 喂：`pipeline_feed()` —— 内部 decode + 等回调 + 送 DE
 *    · 换分辨率：`pipeline_reconfigure()` —— **只重建解码器 + 改源尺寸**
 *    · 关（退出时）：`pipeline_close()` —— 唯一的销毁入口
 *
 *  【★ 为什么换分辨率**不能**用 close+open】
 *    `display_init()` 里有一步会 `pwrite` 把 fb0 的视频区（y=48..528）刷 0 ——
 *    那是给开机用的（让 DE 视频层透出）。但换格式时 Qt 已经把界面画在那块内存上，
 *    刷 0 等于**把界面擦掉一块**，而 Qt 不知道被外部改过 → 不会重绘 → 永久黑屏。
 *    实际踩过：摄像头页点「保存」，分辨率没变，屏幕中间直接黑掉。
 *    所以换格式走 `pipeline_reconfigure()`：不碰 fb0、不动 g_visible。
 *
 *  @note 本模块零 Qt；只有有状态句柄（解码器/图层）在这里被持有，线程安全性
 *        由"只由采集线程调用"来保证，不额外加锁。
 */
#ifndef _PIPELINE_H_
#define _PIPELINE_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建解码器 + 建 DE 图层。
 *
 *  @param srcW,srcH   源（摄像头实际协商出的）分辨率
 *  @param cropY,cropH 源上的裁剪窗口（720p 基准值，AppController 之后会按实际分辨率重算）
 *  @param winX,winY,winW,winH  屏幕上的目标窗口
 *  @return 0 成功；-1 失败（失败时内部已回滚，不需要调用方 cleanup）
 *
 *  @note 若管线已经开着，会先 `pipeline_close()` 再重开。
 */
int pipeline_open(int srcW, int srcH, int cropY, int cropH,
                  int winX, int winY, int winW, int winH);

/** @brief 喂一帧 MJPEG，等解码完成并送显。
 *
 *  @param jpg 一帧 MJPEG 压缩数据的首地址（通常传 @c capture_frame_data()）。
 *             只要求在本次调用期间有效 —— 解码在本函数内同步启动。
 *  @param len 该帧的字节数（通常传 @c capture_frame_size()；
 *             调用方已保证 ≥ 1000，残帧在更上游被丢弃）。
 *  @return 0 = 已送显；-1 = 未开管线 / 解码超时（200ms 内回调没触发）
 *
 *  @note **不负责**归还 V4L2 缓冲 —— 那是调用方的事（capture_release）。
 */
int pipeline_feed(const void *jpg, size_t len);

/** @brief 换分辨率：只重建解码器 + 下发新的源尺寸。
 *
 *  **不重建显示层**（不调 display_init、不清 fb0）、**不动可见性**（g_visible 归 AppController）。
 *  这是换格式唯一该走的路径 —— 详见文件头那段「为什么不能用 close+open」。
 *
 *  @param srcW,srcH **必须传协商后的实际生效值**（`capture_query_format` 读回的
 *                   尺寸），**不能传 UI 上选择的期望值** —— S_FMT 是协商不是命令，
 *                   驱动可能给不了你要的尺寸；拿期望值去 init 解码器 / 配 DE 源尺寸
 *                   会让两边错配（花屏或黑屏）。参照 `CaptureSession::switchFormat` 里
 *                   先 `capture_query_format` 再调本函数的顺序。
 *  @return 0 成功；-1 失败（管线没开 / 解码器重建失败）
 */
int pipeline_reconfigure(int srcW, int srcH);

/** @brief 关管线。**唯一的销毁入口**。
 *
 *  顺序固定：`display_set_visible(0)` → `decoder_destroy()` → `display_destroy()`
 *  可重复调用（第二次开始是空操作）。
 */
void pipeline_close(void);

/** @brief 当前源宽（管线没开时返回 0）。 */
int pipeline_src_w(void);

/** @brief 当前源高（管线没开时返回 0）。 */
int pipeline_src_h(void);

#ifdef __cplusplus
}
#endif

#endif /* _PIPELINE_H_ */
