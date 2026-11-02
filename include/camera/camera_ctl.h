// SPDX-License-Identifier: MIT
/** @file camera_ctl.h
 *  @brief 摄像头 V4L2 控制项读写（亮度 / 对比度 / 白平衡 …）
 *
 *  【为什么不复用采集线程的 fd】
 *  采集线程持有 video_fd 并正在 STREAMON，那条 fd 归它所有（停流、重建、
 *  S_FMT 都在它自己线程里做）。这里只在需要时**单独 open 一次**、做完 ioctl
 *  就 close —— 不共享 fd、不加锁、不跨线程。
 *  【流式期间能读能写吗】
 *  能。uvcvideo 允许多次 open，VIDIOC_G_CTRL / VIDIOC_S_CTRL 在 STREAMON
 *  之后是合法的（实测 13 个控制项都能在出图时写，见 10-摄像头图像控制.md）。
 *  只有 S_FMT / S_PARM 这类会打断流的请求才必须由采集线程自己做。
 *  【返回值】
 *  0 = 成功，-1 = 失败（设备打不开 / ioctl 报错，后者 errno 有意义）。
 *  id 用 linux/videodev2.h 里的 V4L2_CID_*。
 *  【三层概念，别混】
 *  V4L2_CID_xxx                 控制项编号（"要改哪一个"）
 *  VIDIOC_QUERYCTRL(camera_ctl_query)  问它的范围/默认值/标志（"能填多少"）
 *  VIDIOC_G_CTRL / S_CTRL(get/set)     读/写它的当前值（"现在多少"→"改成多少"）
 *  【线程安全】本模块无全局状态，任何线程都能调；
 *  但别在持有采集线程 fd 的地方调（会打架），也不要在信号处理函数里调（open 不安全）。
 */
#ifndef CAMERA_CTL_H
#define CAMERA_CTL_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 控制项操作的设备路径。
 *
 *  @note 写死 /dev/video0 —— 本机只有一颗 UVC 摄像头（capture_init() 扫出来的也是它）。
 *  @warning 换设备时这里要跟着改，否则控制项全部写不进去。
 */
#define CAMERA_CTL_DEV "/dev/video0"

/* =============================================================================
 *  需要按配置持久化的数值控制项
 * =============================================================================
 *  这张表是「哪些控制项要存进 settings.ini、开机要恢复」的**唯一真相源**：
 *    · 相机页点「保存」→ 按它写配置
 *    · 采集线程启动  → 按它下发
 *  两边合用一个表，才不会出现"界面写 camera/gain、开机读别的键"这种静默不一致。
 *
 *  ★ 只放**数值型**（int）项。白平衡自动是 bool、抗工频是 menu，语义和范围都不同，
 *    单独在 capture_thread.cpp 的 apply_camera_ctrls() 里处理。
 *  ★ 表的**顺序**要和 CameraPage.cpp 里 kNumSpecs 的行位置一一对应。
 * ========================================================================== */

/** @brief 一个需要按配置持久化的数值控制项。 */
typedef struct {
    unsigned int cid;    /**< V4L2 控制项 ID */
    const char  *key;    /**< settings.ini 的键（"section/key"） */
    const char  *name;   /**< 中文名，给日志和弹窗标题用 */
} CameraCtlItem;

/** @brief 持久化控制项表。长度见 @ref g_ctlItemCount。 */
extern const CameraCtlItem g_ctlItems[];

/** @brief 表长度。 */
extern const int g_ctlItemCount;

/** @brief 读控制项当前值。
 *
 *  @param id     V4L2 控制项 ID（如 @c V4L2_CID_BRIGHTNESS）。
 *  @param value  输出值；可为 NULL（只想确认"这项存在吗"时）。
 *  @return 0 成功；-1 失败（errno 保留，调用方可判原因）。
 *
 *  @note 典型失败原因：@c EINVAL 这颗摄像头没有该 CID；
 *        @c EBUSY/@c EIO 设备正在掉线；@c ENOENT 摄像头没插。
 */
int camera_ctl_get(unsigned int id, int *value);

/** @brief 写控制项。
 *
 *  @param id     V4L2 控制项 ID。
 *  @param value  目标值。
 *  @return 0 成功；-1 失败。
 *
 *  @warning 写不进去有三个常见原因，都不带明显提示：
 *           - 值超范围（EINVAL）—— 必须先 camera_ctl_query() 拿 min/max
 *           - 该项此刻 INACTIVE（如自动曝光开着时写 exposure_absolute）
 *           - 该项只读（V4L2_CTRL_FLAG_READ_ONLY）
 */
int camera_ctl_set(unsigned int id, int value);

/** @brief 读控制项的元信息（范围 / 步长 / 默认值 / 标志位）。
 *
 *  @param id     V4L2 控制项 ID。
 *  @param min    输出最小值；可为 NULL。
 *  @param max    输出最大值；可为 NULL。
 *  @param step   输出步长；可为 NULL。
 *  @param def    输出默认值；可为 NULL。
 *  @param flags  输出 V4L2_CTRL_FLAG_* 位；可为 NULL。
 *  @return 0 成功；-1 失败（设备打不开 / 控制项不存在 / 被 DISABLED）。
 *
 *  @note 为什么必须有这个：范围**不能硬编码** —— 范围是摄像头报的，换一颗就全错。
 *  @note @p flags 重点看 @c V4L2_CTRL_FLAG_INACTIVE：该项此刻不可调，
 *        但仍可查询（典型：白平衡自动开着时，色温档位是 INACTIVE）。
 *        这和 @c V4L2_CTRL_FLAG_DISABLED 不同 —— 后者本函数直接返回 -1。
 */
int camera_ctl_query(unsigned int id,
                     int *min, int *max, int *step,
                     int *def, unsigned int *flags);

#ifdef __cplusplus

/** @brief 按配置下发全部图像控制项（wbAuto / powerLine / 已配置的数值项）。
 *
 *  【时机】每次开流成功后调一次：启动 / 换格式 / 断线重连。
 *         （UI 点「保存」的**即时**下发不走这里，走 CameraService::ctrlSet；
 *           本函数负责的是重启 / 重连后把配置里的值恢复到硬件上这条恢复路。）
 *
 *  【为什么必须重发】设备刚打开时，控制值可能是上一轮的残留。
 *  数值项只下发配置里**存在**的键（settings_has 区分从没配过与恰为默认值，
 *  没配过的项不碰硬件），且跳过设备标 INACTIVE 的项（写了也 EINVAL）。
 */
int camera_ctl_apply_settings(void);

}
#endif

/* =============================================================================
 *  被裁剪掉的 V4L2 控制项 ID
 * =============================================================================
 *  交叉工具链自带的 videodev2.h 只保留了 V4L2 1.0 的核心定义，下面这三个
 *  camera class 的宏被裁掉了（内核 5.4 本身是支持的）。
 *
 *  ★★ 这里一律用【字面值】，不要改回 (V4L2_CID_CAMERA_CLASS_BASE + n) 这种表达式。
 *
 *  踩过的坑（2026-10-30）：
 *    工具链头文件里 V4L2_CID_CAMERA_CLASS_BASE = 0x009A0900，于是
 *    CAMERA_CLASS_BASE + 12 = 0x009A090C；
 *    但本设备上的"自动白平衡"是 0x0098090C（= V4L2_CID_BASE + 12，
 *    也就是老的 user class 控制 V4L2_CID_AUTO_WHITE_BALANCE，
 *    不是 camera class 那一个）。
 *
 *    写成表达式时结果还取决于"本头文件被包含时 videodev2.h 有没有先被包含"，
 *    因为下面的 #ifndef 组合会走不同分支：
 *      camera_ctl.c        —— 本头文件先行，CLASS_BASE 不可见 → 落到字面值 → 0x0098090C ✓
 *      capture_thread.cpp  —— capture.h（含 videodev2.h）先行 → CLASS_BASE 可见
 *                             → 走表达式 → 0x009A090C ✗
 *    表现就是"程序启动时白平衡自动写不进去（EINVAL），但 v4l2-ctl 能写"。
 *
 *    ★ 这几行原来放在 #endif（include guard）之后，会被重复展开，
 *      也是上面那种"包含顺序决定结果"的帮凶 —— 现在挪进 guard 里面。
 *
 *  值来源：内核 uapi 的 include/uapi/linux/v4l2-controls.h，3.x 起没变过。
 *  校验工具：tests/wb_ctl —— 跑第一段就能看出宏值和设备对不对得上。
 * ========================================================================== */
/* 【为什么还留着 #ifndef】
 * 工具链头文件里 PL_FREQ / EXP_AUTO 这两个**其实已经定义了**，只是值恰好相同
 * （PL_FREQ = V4L2_CID_BASE+24 = 0x00980918；EXP_AUTO = 相机扩展类+1 = 0x009A0901）。
 * 不加 #ifndef 会出 "redefined" 警告（批 2 一度去掉过，主工程立刻两条警告）。
 *
 * ★ 但这里的 #ifndef 只负责「挡重定义」，**不再负责选值** ——
 *   值一律是下面的字面量，不依赖任何别的宏是否可见。
 *   原来那套「先 (CLASS_BASE+12)、再在 #ifndef CLASS_BASE 里 #undef 重定义」的两段式，
 *   正是"包含顺序决定宏值"的根源，已经彻底删掉。 */
#ifndef V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO
#define V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO  0x0098090C  /* bool     自动白平衡开关 */
#endif
#ifndef V4L2_CID_POWER_LINE_FREQUENCY
#define V4L2_CID_POWER_LINE_FREQUENCY            0x00980918  /* menu 0/1/2  抗工频闪烁 */
#endif
#ifndef V4L2_CID_EXPOSURE_AUTO
#define V4L2_CID_EXPOSURE_AUTO                   0x009A0901  /* menu        曝光模式 */
#endif

/* 这三个值的含义，用的时候对照：
 *   0x0098090C  白平衡自动   0=手动  1=自动（写 1 后色温档位会变 INACTIVE）
 *   0x00980918  抗工频闪烁   0=关闭  1=50Hz  2=60Hz（菜单型，写中间值会 EINVAL）
 *   0x009A0901  曝光模式     0=自动  1=手动  2=光圈优先  3=快门优先
 *              本项目固定 1（手动），但实测这颗摄像头只报"手动"这一档。
 */


#endif /* CAMERA_CTL_H */
