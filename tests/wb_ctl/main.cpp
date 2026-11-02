#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* ★ 故意用项目真实的头文件，而不是自己再定义一遍 ——
 *   这个程序的一半价值就是检查 camera_ctl.h 里的宏在**当前包含顺序下**算出来的值对不对。
 *   包含顺序和 capture_thread.cpp 保持一致：先 videodev2.h（经由 capture.h），再 camera_ctl.h。 */
#include "capture.h"
#include "camera_ctl.h"

/* 设备上真实的值（v4l2-ctl --list-ctrls 报的），用来比对 */
#define EXPECT_WB   0x0098090C
#define EXPECT_PLF  0x00980918

#define DEV "/dev/video0"
#define CID V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO

static int g_fd = -1;

/* ---- 读：legacy VIDIOC_G_CTRL ----
 * ★ 关键：调用失败时 outBuf 里是调用者自己填的旧值，不能当读回结果用。
 *   之前我第一次测试就被这个坑过 —— 打印出"读回=1"，其实那次读也失败了。 */
static int get_wb(int *out)
{
    struct v4l2_control c;
    memset(&c, 0, sizeof(c));
    c.id = CID;

    errno = 0;
    const int r = ioctl(g_fd, VIDIOC_G_CTRL, &c);
    if (r == 0 && out)
        *out = c.value;
    return r;
}

/* ---- 写：legacy VIDIOC_S_CTRL（和 camera_ctl_set 完全一样的调用） ---- */
static int set_wb(int v)
{
    struct v4l2_control c;
    memset(&c, 0, sizeof(c));
    c.id    = CID;
    c.value = v;

    errno = 0;
    return ioctl(g_fd, VIDIOC_S_CTRL, &c);
}

static void show(const char *tag, int r, int err)
{
    int v = -999;
    const int rg = get_wb(&v);

    printf("  %-22s 写ret=%-3d errno=%-3d %-16s | 读回ret=%-3d 值=%s\n",
           tag, r, err, strerror(err),
           rg, (rg == 0) ? "" : "(读失败)");
    if (rg == 0)
        printf("  %-22s  → 硬件当前值 = %d\n", "", v);
}

int main(void)
{
    printf("=== 白平衡自动控制项写入实验 ===\n");
    printf("设备 %s\n\n", DEV);

    /* ---- 第 0 步：先核对宏算出来的 CID 和设备上的是不是同一个 ---- */
    const unsigned wbCode  = V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO;
    const unsigned plfCode = V4L2_CID_POWER_LINE_FREQUENCY;
    printf("0. CID 校验（这是本程序最关键的输出）\n");
    printf("   白平衡自动  宏值=0x%08X  设备=0x%08X  %s\n",
           wbCode, EXPECT_WB, (wbCode == EXPECT_WB) ? "✓ 一致" : "✗ 不一致！");
    printf("   抗工频闪烁  宏值=0x%08X  设备=0x%08X  %s\n",
           plfCode, EXPECT_PLF, (plfCode == EXPECT_PLF) ? "✓ 一致" : "✗ 不一致！");
    if (wbCode != EXPECT_WB)
        printf("   ⚠ 宏值不对，下面的写入必然全失败，先修 camera_ctl.h 的宏定义\n");
    printf("\n");

    g_fd = open(DEV, O_RDWR | O_NONBLOCK);
    if (g_fd < 0) {
        printf("打不开设备: %s\n", strerror(errno));
        printf("（程序 work 在跑也没关系，uvcvideo 允许多次 open）\n");
        return 1;
    }

    int v = -999;
    int r = get_wb(&v);
    printf("1. 起始状态\n");
    if (r == 0)
        printf("   读成功，当前 wb_auto = %d\n\n", v);
    else
        printf("   读失败（宏值不对时这也会失败）\n\n");

    /* ---- 实验 A：先置 0，再立刻写 1（复现"连续写失败"） ---- */
    printf("2. 先写 0，再立刻写 1（两次写之间不 sleep）\n");
    errno = 0;
    int ra = set_wb(0);
    show("写 0", ra, errno);

    errno = 0;
    int rb = set_wb(1);
    show("立刻写 1", rb, errno);
    printf("\n");

    /* ---- 实验 B：失败后隔 500ms 重试，最多 3 次 ---- */
    printf("3. 每隔 500ms 重试写 1，最多 3 次\n");
    for (int i = 1; i <= 3; ++i) {
        usleep(500 * 1000);

        errno = 0;
        const int rr = set_wb(1);
        char tag[32];
        snprintf(tag, sizeof(tag), "第 %d 次重试写 1", i);
        show(tag, rr, errno);

        if (get_wb(&v) == 0 && v == 1) {
            printf("\n  ✅ 第 %d 次成功写入（说明失败是瞬时的，可用重试兜住）\n", i);
            break;
        }
        if (i == 3)
            printf("\n  ❌ 3 次都失败：不是瞬时问题，需要查设备状态或换写入方式\n");
    }

    close(g_fd);
    return 0;
}
