#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern "C" {
#include "capture.h"
#include "camera_ctl.h"
}

/* =============================================================================
 *  streamon_ctrl —— 验证「uvc 会在 STREAMON 时重置控制项」这句断言
 * =============================================================================
 *  capture_thread.cpp 里有一句注释：
 *      「开流之后再下发图像控制项：uvc 会在 STREAMON 时重置一部分控制」
 *
 *  这句话决定了「控制项下发的时机」这个设计。但它从来没被验证过。
 *
 *  做法：
 *    1. 把亮度设成一个和默认值明显不同的值（默认 5，设 8）
 *    2. 读一次确认写进去了
 *    3. 协商格式 + 开流（STREAMON）
 *    4. 立刻再读
 *    5. 相同 = 没重置（断言错）；变回 5 = 被重置（断言对）
 *
 *  亮度之外顺带测白平衡自动（它被 AUTO_CLUSTER 管着，行为可能不同）。
 * ========================================================================== */

static void show(const char *tag, unsigned id, int defVal)
{
    int v = -1;
    if (camera_ctl_get(id, &v) == 0)
        printf("  %-14s = %-4d %s\n", tag, v,
               (v == defVal) ? "（等于默认值）" : "（非默认）");
    else
        printf("  %-14s 读失败\n", tag);
}

int main(void)
{
    printf("=== STREAMON 是否重置控制项 ===\n\n");

    int fd = capture_init();
    if (fd < 0) {
        printf("没找到摄像头（先 bash scripts/deploy.sh stop）\n");
        return 1;
    }

    /* ---- ① 设成非默认值 ---- */
    printf("1) 先改成非默认值\n");
    camera_ctl_set(V4L2_CID_BRIGHTNESS, 8);
    camera_ctl_set(V4L2_CID_CONTRAST, 17);
    show("brightness", V4L2_CID_BRIGHTNESS, 5);
    show("contrast",   V4L2_CID_CONTRAST,   10);

    const int brBefore = ({int t = -1; camera_ctl_get(V4L2_CID_BRIGHTNESS, &t); t;});
    const int ctBefore = ({int t = -1; camera_ctl_get(V4L2_CID_CONTRAST,   &t); t;});
    const int wbBefore = ({int t = -1; camera_ctl_get(V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO, &t); t;});

    /* ---- ② 协商 + 开流 ---- */
    printf("\n2) 协商格式 + STREAMON\n");
    if (capture_set_format(fd, 1280, 720, 30) < 0)
        printf("   S_FMT 失败\n");
    unsigned w = 0, h = 0, f = 0;
    if (capture_query_format(fd, &w, &h, &f) == 0)
        printf("   协商结果 %ux%u @%ufps\n", w, h, f);

    if (capture_start(fd) < 0) {
        printf("   开流失败\n");
        close(fd);
        return 1;
    }

    /* ---- ③ 开流后立刻读 ---- */
    printf("\n3) STREAMON 之后立刻读\n");
    show("brightness", V4L2_CID_BRIGHTNESS, 5);
    show("contrast",   V4L2_CID_CONTRAST,   10);
    show("wb_auto",    V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO, 1);

    const int brAfter = ({int t = -1; camera_ctl_get(V4L2_CID_BRIGHTNESS, &t); t;});
    const int ctAfter = ({int t = -1; camera_ctl_get(V4L2_CID_CONTRAST,   &t); t;});
    const int wbAfter = ({int t = -1; camera_ctl_get(V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO, &t); t;});

    /* ---- ④ 结论 ---- */
    printf("\n=== 结论 ===\n");
    printf("  brightness : %d → %d  %s\n", brBefore, brAfter,
           (brBefore == brAfter) ? "【没被重置】" : "【被 STREAMON 重置了】");
    printf("  contrast   : %d → %d  %s\n", ctBefore, ctAfter,
           (ctBefore == ctAfter) ? "【没被重置】" : "【被 STREAMON 重置了】");
    printf("  wb_auto    : %d → %d  %s\n", wbBefore, wbAfter,
           (wbBefore == wbAfter) ? "【没被重置】" : "【被 STREAMON 重置了】");

    const bool anyReset = (brBefore != brAfter) || (ctBefore != ctAfter) || (wbBefore != wbAfter);
    printf("\n  → 注释里「uvc 会在 STREAMON 时重置一部分控制」%s\n",
           anyReset ? "【成立】：确实有控制项被复位，所以必须在开流后下发"
                    : "【不成立】：STREAMON 没有重置任何控制项，那句注释是错的");

    /* ---- 收尾：恢复默认 ---- */
    capture_stop(fd);
    close(fd);
    camera_ctl_set(V4L2_CID_BRIGHTNESS, 5);
    camera_ctl_set(V4L2_CID_CONTRAST,   10);
    printf("\n（已恢复 brightness=5 contrast=10）\n");
    return 0;
}
