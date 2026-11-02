// SPDX-License-Identifier: MIT
/** @file camera_ctl.c
 *  @brief 摄像头控制项（亮度/对比度/白平衡…）的读写实现
 *
 *  接口说明见 camera_ctl.h；这里只讲实现上几个"看着奇怪但必须这样"的点：
 *  ① 每次调用都 open/close 一遍
 *  代价是一次 open（几微秒），换来"绝不和采集线程抢 fd"。
 *  控制是低频操作（人点一下改一次），不值得为它维护长连接或加锁。
 *  ② ioctl 之后、close 之前先把 errno 存起来
 *  close() 自己也可能改 errno（尤其是非阻塞 fd），
 *  不先存的话，调用方拿到的就是 close 的 errno，而不是 ioctl 的 ——
 *  排查时会被误导成"设备打不开"，其实是 EINVAL。
 *  ③ 用 struct v4l2_control + VIDIOC_G_CTRL/S_CTRL（老式单值接口）
 *  而不是 v4l2_ext_controls（新式批量接口）：
 *  单个 int 值的控制项用老接口更短，而且这颗摄像头的工具链头文件里
 *  VIDIOC_G_EXT_CTRLS 的宏虽然存在，但内核 uvc 驱动对 G_CTRL 支持最稳。
 */
#include "camera_ctl.h"

#include "settings_c.h"     /* 读配置（C 面；platform 层不许碰 app_settings.h 的 QString） */
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

/* 每次调用单独 open/close。
 * 代价是一次 open（微秒级），换来的是"绝不和采集线程抢 fd"。
 * 控制类是低频操作（用户点一下改一次），不值得为此维护一条长连接。
 *
 * 【O_NONBLOCK 的意义】uvcvideo 的 open 本身不会阻塞，
 * 但加着它是个保险：万一某次 ioctl 落在"设备正被拔掉"的窗口里，
 * 非阻塞 fd 会立刻返回 EIO，而不是挂住整个 UI 线程。 */
static int open_ctrl_fd(void)
{
    return open(CAMERA_CTL_DEV, O_RDWR | O_NONBLOCK);
}

/* 读一个控制项的当前值。
 * 返回值和参数说明见 camera_ctl.h（合同只写一处，这里只补实现层的 why）。
 *
 * 为什么失败时**保留 ioctl 的 errno** 而不是覆盖成自己的：
 *   EINVAL 表示"这颗摄像头不认这个 CID"，和"设备坏了(ENODEV)"必须分开 ——
 *   前者是能力差异（该跳过），后者该走重连。 */
int camera_ctl_get(unsigned int id, int *value)
{
    int fd = open_ctrl_fd();
    if (fd < 0)
        return -1;

    /* v4l2_control = { id, value }，只有 id 是入参，value 由驱动填。
     * memset 是必须的：结构体里可能有 padding，不初始化会被 valgrind 报未初始化读取，
     * 个别驱动还会检查保留字段是否为 0。 */
    struct v4l2_control c;
    memset(&c, 0, sizeof(c));
    c.id = id;

    const int ret = ioctl(fd, VIDIOC_G_CTRL, &c);
    const int err = errno;          /* ★ 先存 errno，见文件头 ② */
    close(fd);

    if (ret < 0) {
        errno = err;                /* 还原成 ioctl 的 errno 再返回 */
        return -1;
    }
    if (value)
        *value = c.value;           /* 允许调用方传 NULL（只关心成功与否） */
    return 0;
}

/* 写一个控制项。返回值和失败原因见 camera_ctl.h。
 *
 * 为什么"写成功"不等于"马上能读到新值"：
 *   自动曝光的项（如 exposure_absolute）驱动要到下一帧才更新，
 *   这是驱动行为不是 bug —— 所以写完立刻 G_CTRL 读回可能拿到旧值，
 *   不要据此判断"写失败"。 */
int camera_ctl_set(unsigned int id, int value)
{
    /* 【调用链最后一环】UI 改亮度 → CameraService::ctrlSet → 这里。
     *  两条路都会走到本函数：
     *    · UI 点「保存」即时下发（见 CameraPage::onSave 的链路地图）
     *    · 开流成功后按配置重发（camera_ctl_apply_settings） */
    int fd = open_ctrl_fd();
    if (fd < 0)
        return -1;

    struct v4l2_control c;
    memset(&c, 0, sizeof(c));
    c.id    = id;
    c.value = value;

    const int ret = ioctl(fd, VIDIOC_S_CTRL, &c);
    const int err = errno;
    close(fd);

    if (ret < 0)
        errno = err;
    return ret < 0 ? -1 : 0;
}

/* =============================================================================
 *  读控制项元信息
 * =============================================================================
 *  DISABLED 和 INACTIVE 不是一回事：
 *    DISABLED → 内核说"这个控制项不存在/不可用" → 根本别显示，返回 -1
 *    INACTIVE → 存在但此刻不可调 → 仍然返回 0，让调用方自己决定置灰
 *              （典型：白平衡自动开着时，色温档位就是 INACTIVE；
 *                关掉自动白平衡它就变可调了）
 *
 *  【为什么范围一定要问设备】
 *    亮度范围是 0~10 还是 0~255，只有驱动知道；换一颗摄像头就全变。
 *    所以界面上的弹窗范围必须来自这里，不能硬编码（硬编码过的代价见
 *    项目记录 12-摄像头设置页.md 的 5.x 节）。
 * ========================================================================== */
int camera_ctl_query(unsigned int id,
                     int *min, int *max, int *step,
                     int *def, unsigned int *flags)
{
    int fd = open_ctrl_fd();
    if (fd < 0)
        return -1;

    /* v4l2_queryctrl 是"输入 id、输出整套元信息"的结构：
     *   minimum / maximum / step / default_value / flags / type / name
     * 全部由驱动填。本函数故意不返回 type/name —— 上层用不到，
     * 留着只会让人以为"要不要按 type 分支"。 */
    struct v4l2_queryctrl q;
    memset(&q, 0, sizeof(q));
    q.id = id;

    const int ret = ioctl(fd, VIDIOC_QUERYCTRL, &q);
    const int err = errno;
    close(fd);

    if (ret < 0) {
        errno = err;
        return -1;
    }

    /* DISABLED：这个 CID 在这颗设备上根本不存在，调用方不该显示它。
     * （返回 -1 而不是 0 + 标志，是为了让调用方少写一层判断。） */
    if (q.flags & V4L2_CTRL_FLAG_DISABLED)
        return -1;

    /* 五个出参都可传 NULL —— 只想要 flags 的时候不用准备五个变量 */
    if (min)   *min   = q.minimum;
    if (max)   *max   = q.maximum;
    if (step)  *step  = q.step;
    if (def)   *def   = q.default_value;
    if (flags) *flags = q.flags;    /* 调用方重点看 INACTIVE 位 */
    return 0;
}

/* =============================================================================
 *  需要按配置持久化的数值控制项（说明见 camera_ctl.h）
 * =============================================================================
 *  CID 用工具链头文件里现成的宏，已与设备上报值逐个核对（见 16 号文档）。
 *  顺序必须和 CameraPage.cpp 的 kNumSpecs 对齐。
 * ========================================================================== */
const CameraCtlItem g_ctlItems[] = {
    { V4L2_CID_BRIGHTNESS,                "camera/brightness", "亮度"       },
    { V4L2_CID_CONTRAST,                  "camera/contrast",   "对比度"     },
    { V4L2_CID_SATURATION,                "camera/saturation", "饱和度"     },
    { V4L2_CID_GAMMA,                     "camera/gamma",      "伽马"       },
    { V4L2_CID_GAIN,                      "camera/gain",       "增益"       },
    { V4L2_CID_WHITE_BALANCE_TEMPERATURE, "camera/wb_temp",    "色温档位"   },
    { V4L2_CID_SHARPNESS,                 "camera/sharpness",  "锐度"       },
    { V4L2_CID_BACKLIGHT_COMPENSATION,    "camera/backlight",  "逆光补偿"   },
    { V4L2_CID_EXPOSURE_ABSOLUTE,         "camera/exposure",   "手动曝光值" },
    { V4L2_CID_FOCUS_ABSOLUTE,            "camera/focus",      "对焦位置"   },
};

const int g_ctlItemCount = (int)(sizeof(g_ctlItems) / sizeof(g_ctlItems[0]));

/* =============================================================================
 *  按配置下发全部图像控制项
 * ========================================================================== */
int camera_ctl_apply_settings(void)
{
    const int wb = settings_get_int("camera/wbAuto", 1);      /* 1 自动 / 0 手动 */
    const int pl = settings_get_int("camera/powerLine", 1);   /* 0 关 / 1 50Hz / 2 60Hz */

    if (camera_ctl_set(V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO, wb ? 1 : 0) != 0)
        LOGW("cam", "下发白平衡自动失败 errno=%d", errno);
    if (camera_ctl_set(V4L2_CID_POWER_LINE_FREQUENCY, pl) != 0)
        LOGW("cam", "下发抗工频闪烁失败 errno=%d", errno);

    /* ---- 数值项：只下发配置里**真的存在**的键 ----
     *
     * 用 settings_has() 而不是 settings_get_int(key, def)：后者分不清
     * "键从来没有过"和"值恰好等于默认值"。用户没调过的项就**不该碰硬件** ——
     * 强行写默认值会把换过摄像头的画面推到奇怪状态
     * （默认值是照这颗 UVC 的出厂值抄的，换一颗就不成立了）。
     * ★ 这是 2026-10-31 修的一个实测缺陷：改了亮度点保存，写进 ini，重启就丢。 */
    int pushed = 0, absent = 0, skipped = 0;

    for (int i = 0; i < g_ctlItemCount; ++i) {
        const CameraCtlItem *it = &g_ctlItems[i];
        const char *key = it->key;   /* ★ C 字符串直用，别把 QString 拖进平台层 */

        if (!settings_has(key)) {
            ++absent;
            continue;
        }

        /* 先判可调性：INACTIVE 的写了也 EINVAL
         * （白平衡自动开着 → 色温档位 inactive；曝光是自动 → 手动曝光值 inactive） */
        int lo = 0, hi = 0;
        unsigned int flags = 0;
        if (camera_ctl_query(it->cid, &lo, &hi, NULL, NULL, &flags) != 0) {
            LOGI("cam", "跳过 %s：设备没有这个控制项", it->name);
            ++skipped;
            continue;
        }
        if (flags & V4L2_CTRL_FLAG_INACTIVE) {
            ++skipped;
            continue;
        }

        const int v = settings_get_int(key, lo);
        if (camera_ctl_set(it->cid, v) != 0)
            LOGW("cam", "下发 %s=%d 失败 errno=%d", it->name, v, errno);
        else
            ++pushed;
    }

    LOGI("cam", "按配置下发摄像头控制：wbAuto=%d powerLine=%d；"
                "数值项 下发 %d / 无配置 %d / 不可调跳过 %d",
         wb, pl, pushed, absent, skipped);
    return 0;
}
