#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* =============================================================================
 *  ctrl_enum —— 用 V4L2_CTRL_FLAG_NEXT_CTRL 遍历全部控制项
 * =============================================================================
 *  目的：验证「不硬编码 CID 也能列出全部控制项」这个做法在本设备上可行。
 *
 *  【核心技巧】
 *    把 v4l2_queryctrl.id 设成 V4L2_CTRL_FLAG_NEXT_CTRL（不是某个具体 CID），
 *    QUERYCTRL 就会返回**第一个**控制项，并把它自己的 ID 写回 qctrl.id。
 *    然后 qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL 再调，就拿下一个。
 *    直到 ioctl 返回 -1 / errno == EINVAL，表示遍历结束。
 *
 *  ★ 注意是「按位或」不是「赋值」—— qctrl.id 会不断累积之前的 ID 位，
 *    这是内核约定的用法（相当于"找比我大的下一个"）。
 *
 *  顺带验证 VIDIOC_QUERYMENU：menu 类型的控制项每个选项叫什么。
 * ========================================================================== */

static const char *typeName(unsigned int t)
{
    switch (t) {
    case V4L2_CTRL_TYPE_INTEGER:      return "int";
    case V4L2_CTRL_TYPE_BOOLEAN:      return "bool";
    case V4L2_CTRL_TYPE_MENU:         return "menu";
    case V4L2_CTRL_TYPE_BUTTON:       return "button";
    case V4L2_CTRL_TYPE_INTEGER64:    return "int64";
    case V4L2_CTRL_TYPE_CTRL_CLASS:   return "class";
    case V4L2_CTRL_TYPE_STRING:       return "string";
    case V4L2_CTRL_TYPE_BITMASK:      return "bitmask";
    case V4L2_CTRL_TYPE_INTEGER_MENU: return "int-menu";
    default:                          return "?";
    }
}

int main(void)
{
    int fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("打不开 /dev/video0: %s\n", strerror(errno));
        return 1;
    }

    printf("=== NEXT_CTRL 遍历 /dev/video0 ===\n\n");

    struct v4l2_queryctrl q;
    memset(&q, 0, sizeof(q));
    q.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    int nCtrl = 0, nClass = 0, nMenuItems = 0;

    /* 第一次调用如果就失败，说明驱动/内核不支持 NEXT_CTRL */
    if (ioctl(fd, VIDIOC_QUERYCTRL, &q) < 0) {
        printf("第一次 QUERYCTRL 就失败 errno=%d(%s)\n", errno, strerror(errno));
        printf("→ 本设备/内核**不支持** V4L2_CTRL_FLAG_NEXT_CTRL\n");
        close(fd);
        return 2;
    }
    printf("第一次调用成功，说明 NEXT_CTRL 可用\n\n");

    do {
        if (q.type == V4L2_CTRL_TYPE_CTRL_CLASS) {
            /* class 头部：只用来分组显示，不是真实控制项 */
            ++nClass;
            printf("[class] 0x%08x  %s\n", q.id, q.name);
        } else {
            ++nCtrl;
            printf("  0x%08x  %-32s type=%-9s min=%-6d max=%-6d step=%-3d def=%-6d",
                   q.id, q.name, typeName(q.type),
                   q.minimum, q.maximum, q.step, q.default_value);
            if (q.flags & V4L2_CTRL_FLAG_INACTIVE)
                printf(" INACTIVE");
            if (q.flags & V4L2_CTRL_FLAG_READ_ONLY)
                printf(" READ_ONLY");
            printf("\n");

            /* menu 类型：用 VIDIOC_QUERYMENU 逐个问选项名 */
            if (q.type == V4L2_CTRL_TYPE_MENU ||
                q.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
                for (int idx = q.minimum; idx <= q.maximum; ++idx) {
                    struct v4l2_querymenu m;
                    memset(&m, 0, sizeof(m));
                    m.id    = q.id;
                    m.index = (unsigned int)idx;
                    if (ioctl(fd, VIDIOC_QUERYMENU, &m) == 0) {
                        ++nMenuItems;
                        if (q.type == V4L2_CTRL_TYPE_INTEGER_MENU)
                            printf("        %2d: 整数 %lld\n", idx, (long long)m.value);
                        else
                            printf("        %2d: %s\n", idx, m.name);
                    } else {
                        /* ★ 声明了 min..max 但某项取不到 = 该项不存在
                         *   exposure_auto 就是这种情况：range 是 0~3，
                         *   但只有 index=1 能取到 → 驱动只允许"手动" */
                        printf("        %2d: <EINVAL，该项不存在>\n", idx);
                    }
                }
            }
        }
        q.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    } while (ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0);

    const int endErrno = errno;
    close(fd);

    printf("\n=== 汇总 ===\n");
    printf("  控制项      %d 个\n", nCtrl);
    printf("  class 分组  %d 个\n", nClass);
    printf("  菜单选项    %d 个（取到的）\n", nMenuItems);
    printf("  遍历结束原因 errno=%d(%s)  %s\n",
           endErrno, strerror(endErrno),
           endErrno == EINVAL ? "→ 正常结束" : "→ 异常结束");
    return 0;
}
