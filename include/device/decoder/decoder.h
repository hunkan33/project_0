// SPDX-License-Identifier: MIT
/** @file decoder.h
 *  @brief Cedar 硬解封装（MJPEG → NV12）
 */
#ifndef DECODER_H
#define DECODER_H

/* ★ 不要 include "sdklog.h" —— 它把 LOGV/LOGD/LOGI/LOGW/LOGE
 *   重定义成 SDK 的 ALOG*（非 USE_LOGCAT 构建下是空宏）。
 *   谁先 include 了 common/log.h、之后又（直接或间接）include 本头，
 *   它后面的 LOGI/LOGE 就会**悄悄变成空宏**：编译不报错，日志整段消失。
 *
 *   本工程就吃过这个亏：main.cpp 的 include 顺序是"log.h 在前、
 *   本头在后" → 之后新加的两行退出日志在**预处理阶段**就没了，
 *   表现为"改了代码、编译通过、行为没变"，查了半天产物字节才定位到。
 *   本模块已改用 common/log.h 的 LOGx（见 decoder.cpp）。 */
#include "AWVideoDecoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "sc_interface.h"
#include <pthread.h>
using namespace awvideodecoder;

/** @brief 全局唯一的解码器实例（decoder_init 创建，decoder_destroy 删除）。 */
extern AWVideoDecoder *g_dec;

/** @brief 最近一帧 Y 平面的虚拟地址（回调里记录）。 */
extern unsigned char *g_yVirt;

/** @brief "有新帧"标志：回调里置 1，decoder_take_new() 取走后清 0。 */
extern volatile int g_newFrame;

/** @brief 解码完成回调：把 NV12 输出的地址记到全局变量里。
 *
 *  @note 这个对象必须用**文件级静态对象**，不能 new/delete ——
 *        基类 AWVideoDecoderDataCallback 没有虚析构，delete 会 UB。
 */
class AEdecoderCb : public AWVideoDecoderDataCallback
{
public:
    /** @brief 解码器每解出一帧调用一次（在解码线程上下文里）。
     *
     *  @param pkt 输出帧：NV12 格式，含虚拟地址与**物理地址**
     *             （物理地址是给 DE 零拷贝用的）。
     *  @return 0 表示已接手这块 buffer。
     *
     *  @warning 回调里**不要做耗时操作**（写文件、打日志刷屏）——
     *           会直接拖慢解码线程，表现为掉帧。
     */
    virtual int decoderDataReady(AVPacket *pkt);
};

/** @brief 全局回调实例。 @warning 不能 delete（见类注释）。 */
extern AEdecoderCb g_cb;

/** @brief 取 Y 平面的 dma_buf fd（内部缓存，零拷贝送 DE 用）。
 *
 *  @return 成功返回 fd；失败返回 -1。
 *
 *  @note Y 和 UV 返回**同一个** fd —— NV12 两个平面在同一块连续内存里，
 *        UV 起始 = Y 起始 + 0xE1000。所以 DE 只需要一个 fd 就能扫整帧。
 *  @note 内部按虚拟地址缓存 fd：一次转换会新建一个 dma_buf fd，
 *        不缓存的话每秒 30 帧就是每秒 30 个 fd。
 */
int decoder_y_fd(void);

/** @brief 初始化解码器（按源尺寸建输出缓冲）。
 *
 *  @param srcW 源宽（像素），来自格式协商结果。
 *  @param srcH 源高（像素）。
 *  @return 0 成功；-1 失败。
 *
 *  @warning 换分辨率时要 destroy + init **成对**调 —— init 会清 g_yVirt / g_newFrame，
 *           并把缓存的 dma_buf fd 全部 close()。漏掉清理会让主循环拿到旧指针，
 *           表现为 dmesg 里 dma_buf_get failed + disp dma map fail。
 */
int decoder_init(int srcW, int srcH);

/** @brief 解一帧 MJPEG（异步：立刻返回，结果走回调）。
 *
 *  @param jpg MJPEG 压缩数据（来自 capture_frame_data）。
 *  @param len 字节数（来自 capture_frame_size）。
 *  @return **负数表示失败**；0 或正数表示已提交（真正的解码结果在回调里）。
 *
 *  @warning 返回值语义和常见约定相反 —— @c "< 0" 才是失败，
 *           不要写成 @c "> 0 成功"（本头文件的注释曾经长期写反）。
 */
int decoder_decode(const void *jpg, size_t len);

/** @brief 取"是否有新帧"并清标志。
 *
 *  @return 1 = 自上次调用以来解出了新帧；0 = 没有。
 *
 *  @note 取走即清标志 —— 调用方取到 1 之后应当真的把帧送显示，
 *        否则这一帧就丢了（不会重放）。
 */
int decoder_take_new(void);

/** @brief 销毁解码器对象并释放所有缓存（含 close 掉缓存的 dma_buf fd）。
 *
 *  @warning 调它**之前**必须先 display_set_visible(0) —— DE 图层此刻还在引用
 *           旧解码器那块内存，不先关层的话 DE 下一个 vsync 会 dma_buf_get 失败。
 */
void decoder_destroy(void);

#endif /* DECODER_H */
