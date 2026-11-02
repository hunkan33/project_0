// SPDX-License-Identifier: MIT
/** @file camera_info.c
 *  @brief 摄像头"能力表"的采集与持久化
 *
 *  【要解决的三个问题】
 *  1. 这颗摄像头到底支持哪些「格式 × 分辨率 × 帧率」？
 *  枚举一次要几十次 ioctl（每个格式 × 每个尺寸 × 每个帧率），启动时做太慢。
 *  2. 怎么判断"还是那颗摄像头"？
 *  用指纹：card + bus_info + serial。三者一致就认为没换设备。
 *  3. 能力表放哪儿？
 *  /etc/dvr/camera.ini。程序自己写、别的模块（Qt 界面）只读。
 *  【数据流：谁读谁写】
 *  capture_init()
 *  └─ camera_sync_caps(fd)          ← 本文件
 *  读指纹 → 和 camera.ini 比
 *  一致 → 用缓存的模式表，**不枚举**
 *  不一致/无缓存 → 枚举 → 原子写 camera.ini
 *  Qt 界面（设置页 / 摄像头页）
 *  └─ cam_ini_load()                ← 本文件，只读
 *  把下拉框的候选填上
 *  【为什么这层一个 Qt 符号都没有】
 *  capture/ 整个目录是纯 C + 纯 V4L2，方便单独编成测试程序（tests/ 里就有几个）。
 *  camera_info 只依赖 libc，不拉 Qt。
 */
#include "camera_info.h"
#include "log.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/media.h>       /* struct media_device_info / MEDIA_IOC_DEVICE_INFO */
#include <linux/videodev2.h>

/* =============================================================================
 *  小工具
 * ========================================================================== */

/* 去掉首尾空白（含 \n\r\t）。原地修改，返回后字符串长度会变短。
 *
 * 【为什么要自己写 trim】
 *   读 ini 时 fgets 会把行尾的 \n 带进来，而 "card=Web Camera\n" 和
 *   设备报的 "Web Camera" 用 strcmp 比会不一致 → 指纹永远判"变了"。
 *   C 标准库没有 trim，所以自己来。 */
static void str_trim(char *s)
{
    if (!s || !*s)
        return;

    /* ---- 去头部 ---- */
    char *p = s;
    while (*p == ' ' || *p == '\t')
        ++p;
    if (p != s)
        memmove(s, p, strlen(p) + 1);   /* +1 连结尾的 '\0' 一起搬 */

    /* ---- 去尾部（从后往前，遇到第一个非空白就停）---- */
    size_t n = strlen(s);
    while (n > 0) {
        const char c = s[n - 1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        s[--n] = 0;
    }
}

/* 安全拷贝：src 为空/NULL 时写空串，绝不留野指针。
 * 用 snprintf 而不是 strncpy —— strncpy 不保证补 '\0'，是经典的坑。 */
static void str_set(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", (src && src[0]) ? src : "");
}

/* 当前本地时间，"2026-10-31 17:20:05" 格式，写进 ini 的 queried 字段。
 * localtime_r（可重入版）而不是 localtime（返回静态缓冲区，多线程会串）。 */
static void now_str(char *out, size_t cap)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
}

/* =============================================================================
 *  serial：v4l2_capability 里没有，要去 media 设备上问
 * =============================================================================
 *  【为什么非要有 serial】
 *    card 和 bus_info 都可能重复：同型号两颗摄像头，card 都叫 "Web Camera"，
 *    bus_info 都是 "usb-sunxi-ehci-1.2"（插在同一个口上）。只有 serial 能区分
 *    "还是那一颗" 和 "换了另一颗同款"。
 *
 *  【为什么不能直接读 /dev/media0】
 *    这块板子上 tvd（视频解码器）也会注册 media 节点，media0 不一定是摄像头的。
 *    所以要遍历 /dev/media0..7，用 bus_info 和视频节点的 bus_info 对上号
 *    才认（同一个 USB 设备的 v4l2 节点和 media 节点，bus_info 是同一个字符串）。
 *
 *  @return 0 = 拿到了；-1 = 没找到匹配的 media 节点，或设备没报 serial（非 UVC 常见）
 * ========================================================================== */
static int media_serial(const char *busInfo, char *out, size_t cap)
{
    out[0] = 0;

    for (int i = 0; i < 8; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/media%d", i);

        /* 不存在的就跳过。O_NONBLOCK 是习惯性防御：media 节点的 ioctl 不会阻塞，
         * 但万一以后加了轮询类 ioctl，阻塞住就麻烦了。 */
        const int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;

        struct media_device_info mi;
        memset(&mi, 0, sizeof(mi));

        if (ioctl(fd, MEDIA_IOC_DEVICE_INFO, &mi) == 0
            && strcmp(mi.bus_info, busInfo) == 0) {
            /* bus_info 对上了 = 这就是那颗摄像头的 media 节点 */
            str_set(out, cap, mi.serial);
            close(fd);
            return out[0] ? 0 : -1;   /* 设备报了空 serial 也算"没拿到" */
        }
        close(fd);                    /* 不是它，关掉试下一个 */
    }
    return -1;
}

/* 读设备指纹（card / bus / serial）。
 * 返回值：0 成功；-1 连 QUERYCAP 都失败（设备已经不正常了）。
 * serial 拿不到**不算失败** —— 把 serial 留空，指纹退化成 card+bus。 */
int cam_fp_read(int fd, CamFingerprint *fp)
{
    if (!fp)
        return -1;
    memset(fp, 0, sizeof(*fp));        /* 先清零，避免结构体里有栈垃圾 */

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("[caps] VIDIOC_QUERYCAP");
        return -1;
    }
    //复制 card、bus、serial（card/bus 是定长数组，不保证 '\0' 结尾，str_set 里用 %s 会读越界？
    //不会：内核保证这两个字段以 '\0' 结尾，只是可能被截断到 31 字节）
    str_set(fp->card, sizeof(fp->card), (const char *)cap.card);
    str_set(fp->bus,  sizeof(fp->bus),  (const char *)cap.bus_info);

    /* 拿不到 serial 不算失败：有些非 UVC 设备根本不报 */
    if (media_serial((const char *)cap.bus_info, fp->serial, sizeof(fp->serial)) != 0)
        fp->serial[0] = 0;

    return 0;
}

/* 比两个指纹。完全相同返回 1，否则 0。
 * diff 非空时写人类可读差异（给日志用），多个字段不同用"，"连起来。 */
int cam_fp_same(const CamFingerprint *a, const CamFingerprint *b,
                char *diff, size_t diffLen)
{
    if (diff && diffLen)
        diff[0] = 0;
    if (!a || !b)
        return 0;

    /* 顺序有讲究：serial 最能说明"换了个设备"，
     * 所以差异描述里 serial 排最前，正好对上约定的日志格式 */
    struct { const char *name; const char *x; const char *y; } f[3] = {
        { "serial", a->serial, b->serial },
        { "card",   a->card,   b->card   },
        { "bus",    a->bus,    b->bus    },
    };

    int same = 1;
    for (int i = 0; i < 3; ++i) {
        if (strcmp(f[i].x, f[i].y) == 0)
            continue;

        same = 0;
        if (diff && diffLen) {
            const size_t used = strlen(diff);
            /* 留 8 字节余量再追加，避免 snprintf 截断出半个 UTF-8 汉字
             * （截断的字符在终端里会是乱码，日志看着很难受） */
            if (used + 8 < diffLen) {
                snprintf(diff + used, diffLen - used, "%s%s %s → %s",
                         used ? "，" : "",
                         f[i].name,
                         f[i].x[0] ? f[i].x : "(空)",   /* 空字段显示成"(空)"，否则日志里看不出来 */
                         f[i].y[0] ? f[i].y : "(空)");
            }
        }
    }
    return same;
}

/* =============================================================================
 *  枚举
 * ========================================================================== */

/* 往结果数组里追加一条。返回 0 = 已加（或已存在），-1 = 数组满了。
 *
 * 【为什么要去重】
 *   这颗 UVC 摄像头会把同一个「尺寸 + 帧率」列两遍（实测：2592×1944 出现两次）。
 *   不去重的话下拉框里会出现两个一模一样的选项，用户以为能选到不同东西。
 * ========================================================================== */
static int add_mode(CamMode *out, int max, int *n,
                    const char *fourcc, unsigned w, unsigned h, unsigned fps)
{
    /* 去重：这颗摄像头会把同一个尺寸/帧率列两遍 */
    for (int i = 0; i < *n; ++i) {
        if (out[i].width == w && out[i].height == h
            && out[i].fps == fps && strcmp(out[i].fourcc, fourcc) == 0)
            return 0;
    }
    if (*n >= max)
        return -1;                    /* 数组满：调用方会立刻 return n */

    /* fourcc 是 4 个字节、没有 '\0'，所以用 %s 安全（snprintf 会自己补 '\0'）。
     * 注意 out[*n].fourcc 是 char[5]。 */
    snprintf(out[*n].fourcc, sizeof(out[*n].fourcc), "%s", fourcc);
    out[*n].width  = w;
    out[*n].height = h;
    out[*n].fps    = fps;
    ++(*n);
    return 0;
}

/* 枚举设备支持的全部模式。
 *
 * 【V4L2 的三层枚举协议】（这段是理解本函数的关键）
 *   枚举是"靠 index 递增 + 直到 ioctl 返回 <0"来做的，没有"总数"接口：
 *
 *     VIDIOC_ENUM_FMT            index=0,1,2…      列出支持的像素格式（MJPG / YUYV）
 *       └ VIDIOC_ENUM_FRAMESIZES index=0,1,2…      该格式支持的尺寸
 *           └ VIDIOC_ENUM_FRAMEINTERVALS index=0…  该尺寸支持的帧间隔（帧率的倒数）
 *
 *   任何一层 ioctl 失败（EINVAL）= 这一层枚举到头了 → break 到上一层。
 *   所以看到三个 for(;;) 不要怕，它们就是三层"列表遍历"。
 *
 * 【只展开 DISCRETE】
 *   尺寸/帧率有三种类型：DISCRETE（枚举列出）、STEPWISE（给 min/max/step）、
 *   CONTINUOUS（给 min/max）。后两种穷举出来是几百项，下拉框放不下也没法选，
 *   所以只登记一条日志跳过。
 *
 * 返回条数；<0 表示参数错误（out 为空 / max<=0）。 */
int cam_enum_modes(int fd, const char *onlyFourcc, CamMode *out, int max)
{
    if (!out || max <= 0)
        return -1;

    int n = 0;
    int skipped_stepwise = 0;          /* 统计被跳过的步进/连续型尺寸个数 */

    for (int fi = 0; ; ++fi) {
        struct v4l2_fmtdesc fdesc;
        memset(&fdesc, 0, sizeof(fdesc));
        fdesc.index = (unsigned)fi;
        fdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(fd, VIDIOC_ENUM_FMT, &fdesc) < 0)
            break;                       /* 枚举完了 */

        /* pixelformat 同样是 4 字节无 '\0'，手工拼成字符串 */
        char fourcc[5];
        memcpy(fourcc, &fdesc.pixelformat, 4);
        fourcc[4] = 0;

        if (onlyFourcc && strcmp(fourcc, onlyFourcc) != 0)
            continue;                    /* 调用方只要某一种格式时，其余整层跳过 */

        for (int si = 0; ; ++si) {
            struct v4l2_frmsizeenum fsize;
            memset(&fsize, 0, sizeof(fsize));
            fsize.index        = (unsigned)si;
            fsize.pixel_format = fdesc.pixelformat;   /* ★ 这一层要指明"哪个格式的尺寸" */

            if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsize) < 0)
                break;                   /* 这个格式的尺寸列完了 */

            unsigned w, h;
            if (fsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                w = fsize.discrete.width;
                h = fsize.discrete.height;
            } else {
                /* STEPWISE / CONTINUOUS：组合数量由 step 决定，穷举出来是几百项，
                 * 下拉框放不下也没法选。只记一笔日志，不展开。 */
                ++skipped_stepwise;
                continue;
            }

            int got_fps = 0;             /* 这个尺寸下有没有枚举到至少一个帧率 */

            for (int ii = 0; ; ++ii) {
                struct v4l2_frmivalenum fival;
                memset(&fival, 0, sizeof(fival));
                fival.index        = (unsigned)ii;
                fival.pixel_format = fdesc.pixelformat;
                fival.width        = w;   /* ★ 这一层要指明"哪个尺寸的帧率" */
                fival.height       = h;

                if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fival) < 0)
                    break;               /* 这个尺寸的帧率列完了 */

                unsigned fps = 0;
                if (fival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                    /* timeperframe 是"每帧多少秒"的分数：1/30 → 30fps */
                    const unsigned num = fival.discrete.numerator;
                    const unsigned den = fival.discrete.denominator;
                    if (num && den)
                        fps = (den + num / 2) / num;
                } else {
                    /* 步进型也至少有 min 值，用它当代表值（UI 上只显示一档） */
                    const unsigned num = fival.stepwise.min.numerator;
                    const unsigned den = fival.stepwise.min.denominator;
                    if (num && den)
                        fps = (den + num / 2) / num;
                }

                if (add_mode(out, max, &n, fourcc, w, h, fps) < 0)
                    return n;            /* 数组满：把已经收到的先交出去 */
                got_fps = 1;
            }

            /* 驱动对这个尺寸一个帧率都不报 —— 记一条 fps=0，别把尺寸丢掉
             * （有些驱动就是不给 ENUM_FRAMEINTERVALS，但格式明明能用） */
            if (!got_fps) {
                if (add_mode(out, max, &n, fourcc, w, h, 0) < 0)
                    return n;
            }
        }
    }

    if (skipped_stepwise)
        LOGW("caps", "有 %d 个「步进/连续」型尺寸未展开（只处理离散尺寸）", skipped_stepwise);

    return n;
}

/* =============================================================================
 *  camera.ini 读写
 * =============================================================================
 *  格式长这样（注释以 ; 或 # 开头）：
 *
 *      ; 摄像头能力表 —— 程序自动生成
 *      [camera]
 *      version=1
 *      card=Web Camera
 *      bus=usb-sunxi-ehci-1.2
 *      serial=20260815
 *      queried=2026-10-30 18:20:05
 *      count=11
 *      mode00=MJPG 3840 2160 30
 *      mode01=MJPG 2592 1944 30
 *
 *  自己写一个几十行的解析而不是引库：文件格式就这么点，没必要为它拉依赖。
 * ========================================================================== */

/* 读 camera.ini。
 *
 * 【返回值】文件里有效的 mode 条数（0 = 文件不存在 / 没有内容）。
 * 【关键细节】modes 传 NULL 时**仍然要数出条数** —— 调用方靠这个数判断
 *   "有没有可用的缓存"，不需要真的把 11 条模式拷出来（省一次大数组拷贝）。
 *   所以下面 count 的 ++ 在 if (modes) 外面。 */
int cam_ini_load(const char *path, CamFingerprint *fp, CamMode *modes, int max)
{
    if (fp)
        memset(fp, 0, sizeof(*fp));

    FILE *f = fopen(path, "r");
    if (!f)
        return 0;                    /* 文件不存在不是错误，就是"没缓存" */

    int count = 0;
    char line[160];                  /* 一行最多 160 字节，够长（最长的是 modeNN 行）*/

    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        /* 空行 / 注释 / 段名 都跳过。段名（[camera]）不参与解析 ——
         * 只有一段，没必要做段感知。 */
        if (!line[0] || line[0] == ';' || line[0] == '#' || line[0] == '[')
            continue;

        char *eq = strchr(line, '=');
        if (!eq)
            continue;                /* 不是 key=value 的行，忽略 */

        *eq = 0;                     /* 把这个 '=' 切断，左边是 key、右边是 value */
        char *k = line;
        char *v = eq + 1;
        str_trim(k);
        str_trim(v);

        if (fp) {
            /* 指纹四个字段。用 continue 而不是 else-if 链，
             * 让"匹配到就结束这一轮"更直观 */
            if (!strcmp(k, "card"))    { str_set(fp->card,    sizeof(fp->card),    v); continue; }
            if (!strcmp(k, "bus"))     { str_set(fp->bus,     sizeof(fp->bus),     v); continue; }
            if (!strcmp(k, "serial"))  { str_set(fp->serial,  sizeof(fp->serial),  v); continue; }
            if (!strcmp(k, "queried")) { str_set(fp->queried, sizeof(fp->queried), v); continue; }
        }

        /* modeNN=MJPG 1920 1080 30 —— 前 4 个字符是 "mode" 就算 */
        if (!strncmp(k, "mode", 4)) {
            char fcc[8];
            unsigned w, h, fps;
            /* %7s 限 7 字符，防止超长 fourcc 撑爆 fcc[8] */
            if (sscanf(v, "%7s %u %u %u", fcc, &w, &h, &fps) == 4) {
                if (modes && count < max) {
                    /* 只有真正要数据、且还有空间时才写 */
                    snprintf(modes[count].fourcc, sizeof(modes[count].fourcc), "%s", fcc);
                    modes[count].width  = w;
                    modes[count].height = h;
                    modes[count].fps    = fps;
                }
                ++count;        /* modes 为 NULL 时也要数出来，调用方靠它判断有没有缓存 */
            }
        }
    }

    fclose(f);
    return count;
}

/* 原子写 camera.ini：
 *   1. 先写 <path>.tmp
 *   2. fflush + fsync 把数据真正刷到介质
 *   3. rename 覆盖正式文件（同目录 rename 是原子的）
 *
 * 【为什么非要这么麻烦】
 *   直接 fopen(path,"w") 会在打开瞬间把旧文件清空。
 *   如果这时候掉电/断电，你就得到 0 字节的 camera.ini —— 能力表丢了，
 *   下次启动要重新枚举（慢，而且如果设备没插着，界面就什么选项都没有）。
 *   用 tmp + rename，最坏情况只是这次更新丢了，旧文件完好。
 *
 * 返回 0 成功；-1 失败（失败时会清掉 tmp）。 */
int cam_ini_save(const char *path, const CamFingerprint *fp,
                 const CamMode *modes, int count)
{
    if (!path || !fp)
        return -1;

    /* 目录可能还没建（首次上电，除了这一份没有别的文件） */
    char dir[128];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;                  /* 把 "/etc/dvr/camera.ini" 截成 "/etc/dvr" */
        mkdir(dir, 0755);            /* 已存在会失败（EEXIST），不管 */
    }

    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        perror("[caps] fopen");
        return -1;
    }

    /* queried 为空时自动填当前时间（调用方不用自己维护这个字段） */
    char qbuf[24];
    const char *q = fp->queried;
    if (!q[0]) {
        now_str(qbuf, sizeof(qbuf));
        q = qbuf;
    }

    fprintf(f, "; 摄像头能力表 —— 程序自动生成，手改无效（下次启动会被覆盖）\n");
    fprintf(f, "; 指纹 = card + bus + serial；三者任一变化就重新枚举\n");
    fprintf(f, "[camera]\n");
    fprintf(f, "version=1\n");                 /* 格式版本号，以后改格式要升它 */
    fprintf(f, "card=%s\n",    fp->card);
    fprintf(f, "bus=%s\n",     fp->bus);
    fprintf(f, "serial=%s\n",  fp->serial);
    fprintf(f, "queried=%s\n", q);
    fprintf(f, "count=%d\n",   count);

    for (int i = 0; i < count; ++i) {
        /* %02d = 补零到两位，读的时候不依赖顺序，纯给人看的整齐 */
        fprintf(f, "mode%02d=%s %u %u %u\n", i, modes[i].fourcc,
                modes[i].width, modes[i].height, modes[i].fps);
    }

    /* 原子写：临时文件 → fsync → rename。掉电只会丢这次更新，
     * 不会留下"count=12 但只有 3 行 mode"的半截文件。 */
    if (fflush(f) != 0 || fsync(fileno(f)) != 0)
        perror("[caps] fsync");

    if (fclose(f) != 0) {
        perror("[caps] fclose");
        unlink(tmp);                 /* 写失败就别留垃圾文件 */
        return -1;
    }

    if (rename(tmp, path) != 0) {
        perror("[caps] rename");
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* =============================================================================
 *  指纹比对 + 按需重新枚举（capture_init 调的入口）
 * =============================================================================
 *  【三条分支】
 *    指纹一致 + 有缓存   → 直接用缓存（启动快，跳过几十次 ioctl）
 *    指纹不一致（换了设备）→ 重新枚举，日志里说明"谁变了"
 *    没有缓存（首次上电） → 重新枚举
 *
 *  返回值和分支说明见 camera_info.h（合同只写一处）。
 *  ★ 这里补一句 why：返回条数而不是 0/1，是因为调用方（界面）要用它
 *    决定下拉框填几项 —— 只告诉"成功/失败"没法用。
 * ========================================================================== */
int camera_sync_caps(int fd, const char *devPath)
{
    CamFingerprint cur, old;
    CamMode modes[CAM_MAX_MODES];        /* 11 条 × 20 字节，栈上放得下 */
    const char *dev = devPath ? devPath : "设备";

    if (cam_fp_read(fd, &cur) != 0)
        return -1;

    /* 只数个数量，不把 modes 读出来 —— 命中缓存时省一次大数组拷贝
     * （第三个参数传 NULL，第四个传 0 是占位，不参与逻辑） */
    const int cached = cam_ini_load(CAMERA_INI_PATH, &old, NULL, 0);

    if (cached > 0 && cam_fp_same(&cur, &old, NULL, 0)) {
        LOGI("caps", "%s 指纹一致（serial %s），用缓存 %d 条，跳过枚举", dev, cur.serial[0] ? cur.serial : "未报", cached);
        return cached;
    }

    if (cached > 0) {
        /* 换了摄像头。日志格式按约定："[caps] 设备变了：serial A → B，重新枚举" */
        char diff[192];
        /* 参数顺序是"旧 → 新"：文件里记的是旧的，刚读到的是新的 */
        cam_fp_same(&old, &cur, diff, sizeof(diff));
        LOGW("caps", "设备变了：%s，重新枚举", diff);
    } else {
        LOGI("caps", "%s 没有可用缓存，开始枚举", dev);
    }

    const int n = cam_enum_modes(fd, NULL, modes, CAM_MAX_MODES);
    if (n < 0) {
        LOGE("caps", "枚举失败");
        return -1;
    }

    now_str(cur.queried, sizeof(cur.queried));

    /* ★ 写失败不影响本次使用：内存里的 modes 已经枚举好了，
     *   下次启动还会重新枚举（只是慢一点）。所以只是报警、不返回错误。 */
    if (cam_ini_save(CAMERA_INI_PATH, &cur, modes, n) == 0)
        LOGI("caps", "枚举完成 %d 条，已写入 %s", n, CAMERA_INI_PATH);
    else
        LOGW("caps", "枚举完成 %d 条，但写 %s 失败", n, CAMERA_INI_PATH);

    return n;
}
