// SPDX-License-Identifier: MIT
/** @file capture.c
 *  @brief V4L2 采集的底层封装（开设备 / 协商格式 / 出帧 / 停流）
 *
 *  【这一层在地图上的位置】
 *  capture_thread.cpp   采集线程：取帧 → 送解码 → 更新显示（业务节奏）
 *  ↓ 调用
 *  capture.c（本文件）  只管 V4L2：buffer、poll、DQBUF/QBUF、mmap
 *  ↓ 调用
 *  ioctl(/dev/video0)   内核 uvcvideo 驱动
 *  本文件**不认识**解码器、不认识 Qt、不认识显示层 —— 换掉上层它照样能用。
 *  【一个 V4L2 采集的最小生命周期】
 *  open("/dev/videoN")
 *  → VIDIOC_QUERYCAP    这台设备能不能采集？是不是 UVC？
 *  → REQBUFS            请驱动分配 N 块缓冲
 *  → QUERYBUF + mmap    把每块缓冲映射进本进程地址空间
 *  → QBUF               把空缓冲交给驱动（"这块你可以往里写"）
 *  → STREAMON           开流，摄像头开始往里灌数据
 *  ── 循环 ──
 *  → poll               等"有帧可读"
 *  → DQBUF              取出一块填好的（此刻它归你）
 *  → 处理数据
 *  → QBUF               还回去（"这块我用完了"）
 *  ── 结束 ──
 *  → STREAMOFF          停流
 *  → munmap             解除映射
 *  → REQBUFS(count=0)   让驱动把缓冲也收回去
 *  【为什么全用全局变量存 buffer 状态】
 *  这个文件是"C 风格的单例"：整机只有一个摄像头、一条采集流。
 *  状态（g_bufs / g_cur_idx …）用 static 全局，省掉一个 struct 参数到处传。
 *  ★ 代价是**这些变量只有采集线程在碰** —— 除 mmap 表在 start/stop 时改，
 *  别在别的线程调 capture_start/stop，否则会打穿（没有加锁）。
 */
#include <capture.h>
#include "log.h"
#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include "camera_info.h"
#include "settings_c.h"   /* 期望值/写回实际值（C 面） */

/* /dev/videoN 最多扫到 N-1。这块板子上一般只有 video0（UVC）和视频解码器节点。 */
#define MAX_VIDEO_DEV 64

/* 申请几块缓冲。4 是 V4L2 采集的常用值，理由见 capture_start() 里的注释。
 * ★ 这个数也是 g_bufs / g_bufs_len 两个数组的长度 —— 改它不用担心越界，
 *   但 REQBUFS 万一给回更多块（驱动有可能会），后面的循环会写越界，
 *   所以下面用 req.count 循环时要先确认它没超过 MAX_BUF。 */
#define MAX_BUF 4

/* ---- 采集缓冲的"账本"（全局单例，只在采集线程里读写）---- */
static void  *g_bufs[MAX_BUF]     = {0};   /* mmap 后的虚拟地址（每块缓冲的首地址） */
static size_t g_bufs_len[MAX_BUF] = {0};   /* 每块缓冲的容量（不是帧大小！）*/
static int    g_buf_count         = 0;     /* 实际拿到几块 */
static int    g_cur_idx           = -1;    /* 当前"借出"给调用方的下标，-1=没借出 */
static size_t g_cur_size          = 0;     /* 当前这帧的真实字节数（bytesused）*/

/* =============================================================================
 *  找设备 + 认设备
 * =============================================================================
 *  返回一个**已经打开、且确认能采集的 UVC 设备** fd；找不到返回 -1。
 *
 *  为什么要扫 0..63：
 *    /dev/videoN 的编号是内核按注册顺序分的，不一定 UVC 就在 video0
 *    （这块板子上 tvd 解码器、ISP 之类的也可能占号）。所以只能一个个试。
 *
 *  为什么要判 driver == "uvcvideo"：
 *    原来判的是 cap.card 里含 "USB"，但卡名是厂商随便起的 ——
 *    旧摄像头叫 "USB Camera" 能命中，现在这颗叫 "Web Camera: Web Camera" 就漏了。
 *    driver 字段由驱动自己填，uvcvideo 才是"这是 UVC 摄像头"的可靠判据。
 * ========================================================================== */
int capture_init(void){
     for (int i = 0; i < MAX_VIDEO_DEV; i++) {
        char path[32]={0};
        snprintf(path, sizeof path, "/dev/video%d", i);

        /* O_NONBLOCK 必须加：否则 poll/DQBUF 在没帧时会一直阻塞，
         * 采集线程就没法响应 g_running 退出和"换格式"请求。 */
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            continue;              /* 这个号没设备（ENOENT），试下一个 */
        }
        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            close(fd);
            continue;              /* 打开了但不是 v4l2 设备 */
        }
        //旧兼容：V4L2 1.0 之前没有 device_caps 字段。
        //老驱动只在 capabilities 里报"我能采集"（那时还没有多平面/m2m 这些概念），
        //新驱动用 V4L2_CAP_DEVICE_CAPS 标志说明"请看我 device_caps"。
        //两种都要认，否则老设备会被判成"不能采集"。
        __u32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                   ? cap.device_caps : cap.capabilities;
        //是否有采集能力（单平面 / 多平面都算）
        //sunxi-tvd 这类解码器节点只有 V4L2_CAP_VIDEO_M2M 之类，不含这两个，会被排除。
        if (!(caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
            close(fd);
            continue;
        }
        /* 只认 UVC 摄像头——判驱动名，不判卡名
         * 卡名是厂商随手起的：旧摄像头叫 "USB Camera" 能命中，
         * 新摄像头叫 "Web Camera: Web Camera"，strstr(card,"USB") 就找不到了。
         * driver 字段由驱动自己填，uvcvideo 才是 "这是 UVC 设备" 的可靠判据。
         * sunxi-tvd 的 driver 是 "sunxi-tvd"，自然被排除。 */
        if (strcmp((const char *)cap.driver, "uvcvideo") != 0) {
            close(fd);
            continue;
        }

        /* ---- 认下这个设备之后，先同步一次能力表 ----
         * 指纹（card + bus + serial）和 /etc/dvr/camera.ini 里一致
         *   → 直接用缓存，跳过几十次 ENUM ioctl
         * 不一致（换了摄像头）/ 没有缓存
         *   → 重新枚举并覆盖 camera.ini
         * 串口 / 界面读的都是这个文件，所以"硬件实际支持什么"只有一份答案。 */
        camera_sync_caps(fd, path);

        return fd;      /* ★ 成功路径：fd 一直开着交给采集线程 */
    }
    LOGI("cap", "找不到可用的采集设备");
    return -1;
}

/* =====================================================================
 *  请求采集格式
 * =====================================================================
 *  S_FMT 是【协商】不是命令：你说 1920×1080，驱动可能给你 1280×720。
 *  所以这里只负责"提要求"，到底生效了什么，必须用 capture_query_format()
 *  读回来。调用方拿到实际值之后，要把它写回配置文件 —— 这样界面显示的、
 *  文件里存的、硬件真正跑的，三者才是同一个数。
 *
 *  帧率不走 S_FMT（S_FMT 里没有帧率字段），走 S_PARM 的 timeperframe。
 *  UVC 摄像头对帧率的支持很窄（本项目这颗 MJPG 只枚举了 30fps），
 *  设不进去也不报错，只能靠读回确认。
 *
 *  【什么时候能调】必须在 STREAMON 之前。流跑起来时 S_FMT 会返回 EBUSY
 *  （V4L2 规定：buffer 已分配/正在流式时不允许改格式）。
 *  换格式的正确姿势见 capture_stop() 里那三步。
 * ===================================================================== */
int capture_set_format(int fd, unsigned w, unsigned h, unsigned fps)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;  /* 单平面采集 */
    fmt.fmt.pix.width       = w;                            /* 期望宽 */
    fmt.fmt.pix.height      = h;                            /* 期望高 */
    fmt.fmt.pix.pixelformat = CAP_FMT;                      /* MJPEG */
    /* 场模式：这块屏和这颗 USB 摄像头都是逐行扫描，没有隔行。
     * 传 NONE 是"不关心"，部分驱动会把它改写成自己支持的值。 */
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        /* 常见原因：此时还有 buffer 没归还（EBUSY）、或者请求的格式设备不支持（EINVAL） */
        perror("VIDIOC_S_FMT");
        return -1;
    }

    /* fmt 是"输入 + 输出"参数：传进去的是请求，回来的是**实际生效**的。
     * 所以这里打印的就是协商结果，不是我们请求的那个。 */
    LOGI("cap", "请求 %ux%u @%ufps → 协商 %ux%u fourcc=%c%c%c%c sizeimage=%u", w, h, fps,
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           fmt.fmt.pix.pixelformat & 0xff,          /* fourcc 拆成 4 个字符打印， */
           (fmt.fmt.pix.pixelformat >> 8) & 0xff,   /* 直接 %s 会因为不是字符串 */
           (fmt.fmt.pix.pixelformat >> 16) & 0xff,  /* 而读到垃圾（fourcc 没有 0 结尾）*/
           (fmt.fmt.pix.pixelformat >> 24) & 0xff,
           fmt.fmt.pix.sizeimage);   /* sizeimage：驱动按当前格式估算的单帧最大字节数 */

    /* ---- 帧率：先问设备支不支持设间隔，支持才设 ---- */
    if (fps > 0) {
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        /* capability 里没有 V4L2_CAP_TIMEPERFRAME 的驱动，S_PARM 是空操作，
         * 设了也不会报错 —— 那是最坏情况（你以为设上了，其实没有），
         * 所以先 G_PARM 看能力位，再看要不要 S_PARM。 */
        if (ioctl(fd, VIDIOC_G_PARM, &parm) == 0
            && (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
            /* timeperframe = 每帧多少秒，用分数表示：
             *   1/30 → 30fps；1/15 → 15fps
             * 分子的 1 表示"一帧"，不是"一秒钟一帧"。 */
            parm.parm.capture.timeperframe.numerator   = 1;
            parm.parm.capture.timeperframe.denominator = fps;
            if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0)
                perror("[capture] VIDIOC_S_PARM");
        } else {
            LOGW("cap", "设备不支持设帧间隔，帧率由驱动决定");
        }
    }
    return 0;
}

/* =====================================================================
 *  读回设备当前生效的格式
 * =====================================================================
 *  三个出参都可以传 NULL（只关心其中某一项时）。
 *  fps 换算不出来时写 0 —— 调用方要把 0 当"驱动没报"，不要当 0fps。
 * ===================================================================== */
int capture_query_format(int fd, unsigned *w, unsigned *h, unsigned *fps)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* G_FMT 是纯查询，不会改动设备状态，流跑着也能调。 */
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        return -1;
    }
    if (w) *w = fmt.fmt.pix.width;
    if (h) *h = fmt.fmt.pix.height;

    if (fps) {
        *fps = 0;                       /* 先给个"没读到"的默认值 */
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(fd, VIDIOC_G_PARM, &parm) == 0) {
            /* timeperframe 是"每帧多少秒"的分数：1/30 表示 30fps */
            const unsigned num = parm.parm.capture.timeperframe.numerator;
            const unsigned den = parm.parm.capture.timeperframe.denominator;
            /* num 或 den 为 0 说明驱动没填（UVC 在没开流时常常是 0/0），
             * 直接算会除零崩溃，所以必须判。 */
            if (num > 0 && den > 0)
                *fps = (den + num / 2) / num;      /* 四舍五入到整数（9000/1001 ≈ 9）*/
        }
    }
    return 0;
}

/* =====================================================================
 *  换格式请求：UI 线程写，采集线程读
 * =====================================================================
 *  只存一个最新请求（连点两次以最后一次为准）——
 *  中间那些无意义，换一次格式要停流、重建解码器和 DE 图层，很重。
 *
 *  【为什么需要这个中转】
 *    UI 线程不能自己 S_FMT：video_fd、解码器、DE 图层都归采集线程，
 *    而且 S_FMT 会打断正在跑的流。所以 UI 只"登记意图"，
 *    采集线程在下一轮主循环开头 take 走再执行（见 switch_format）。
 *
 *  锁只保护这三个变量，临界区里只做赋值 —— 采集线程 take 时同样只做赋值，
 *  不会持锁做 ioctl，所以不会卡住 UI。
 * ===================================================================== */
static pthread_mutex_t g_req_lock = PTHREAD_MUTEX_INITIALIZER;
static int      g_req_pending = 0;      /* 有没有挂起的请求 */
static unsigned g_req_w = 0, g_req_h = 0, g_req_fps = 0;

/* 登记一个换格式请求。w/h 必须非 0；fps 传 0 = 不改帧率。
 * 返回 0 只表示"登记上了"，不代表已经切成功。 */
int capture_request_format(unsigned w, unsigned h, unsigned fps)
{
    if (w == 0 || h == 0)          /* 0×0 是无意义请求，直接拒 */
        return -1;

    pthread_mutex_lock(&g_req_lock);
    g_req_w       = w;
    g_req_h       = h;
    g_req_fps     = fps;
    g_req_pending = 1;
    pthread_mutex_unlock(&g_req_lock);
    return 0;
}

/* 取走挂起的请求（有则返回 1 并清标志）。取走之后请求就不存在了，
 * 所以调用方必须真的把它执行掉，否则这次请求就丢了。 */
int capture_take_format_request(unsigned *w, unsigned *h, unsigned *fps)
{
    pthread_mutex_lock(&g_req_lock);

    const int has = g_req_pending;
    if (has) {
        if (w)   *w   = g_req_w;
        if (h)   *h   = g_req_h;
        if (fps) *fps = g_req_fps;
        g_req_pending = 0;
    }
    pthread_mutex_unlock(&g_req_lock);
    return has;
}

/* =====================================================================
 *  开流：分配缓冲 → 映射 → 全部入队 → STREAMON
 * =====================================================================
 *  【前置条件】格式已经用 capture_set_format() 设好。
 *  本函数现在**不碰格式**（老版本在这里顺带 S_FMT，现在拆开了）。
 * ===================================================================== */
int capture_start(int fd)
{

    /* ---- REQBUFS：让驱动分配 buffer ----
       4 块的理由：
         一块正在被 DMA 写（摄像头）
         一块正在被你读（DQBUF 出来的）
         一块在队列里等
         一块余量（防止队列空） */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = 4;                          /* 期望块数，驱动可能少给 */
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;           /* 内核分配、我们 mmap 进来（零拷贝）*/

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    g_buf_count = req.count;                 /* 回来的是实际给了几块 */
        for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index  = i;                          /* 第几块 */
        //查询实际缓存区的状态并记录：主要是拿 length（长度）和 m.offset（内核里的偏移）
        if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }
        /* 把内核里那块物理内存映射到本进程的虚拟地址空间。
         * 之后读写 g_bufs[i] 就是直接读摄像头 DMA 写进去的内容，没有第二次拷贝。
         * MAP_SHARED：驱动写 = 我们也能看到（不能是 MAP_PRIVATE）。 */
        g_bufs[i] = mmap(NULL, b.length,
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, b.m.offset);

        //指针又读又写并共享；MAP_FAILED 是失败时的固定返回值（不是 NULL）
        if (g_bufs[i] == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    g_bufs_len[i] = b.length;//记录容量大小（每块缓冲最大能装多少），不是帧大小
    LOGI("cap", "buffer[%d] %zu KB", i, b.length / 1024);
    }

    //把控制权交给驱动：把 4 块空缓冲全部入队，驱动才有地方写
    for (int i = 0; i < req.count; i++) {
    struct v4l2_buffer b;
    memset(&b, 0, sizeof(b));
    b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    b.index  = i;

    if (ioctl(fd, VIDIOC_QBUF, &b) < 0) {
        perror("VIDIOC_QBUF");
        return -1;
         }
    }
    //通知驱动开始流式传输（这一步之后摄像头才开始真正出图）
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    LOGI("cap", "开始采集");
    return 0;
}

/* =============================================================================
 *  取一帧（出队）
 * =============================================================================
 *  返回值语义（调用方按这个分支，别自己猜）：
 *
 *    0   拿到一帧。帧起始地址 capture_frame_data()、长度 capture_frame_size()。
 *   -2   这一轮没帧：poll 超时（1 秒）或被信号打断。
 *        ★ 这【不是错误】—— 采集线程只记数继续，不重连、不退出。
 *        USB 带宽不够时会短暂出不来帧，当成错误处理会导致误重连。
 *   -1   真出错了：
 *          · poll 报 POLLERR/POLLHUP/POLLNVAL → 摄像头掉线
 *          · DQBUF 失败
 *        调用方应当走重连流程（关 fd → 重开 → 重新协商）。
 *
 *  为什么超时给 -2 而不是 -1：
 *    1 秒没帧很可能是瞬时的（设备忙、带宽抖），重连一次的代价远大于等下一轮。
 *
 *  【借出语义】成功时这块缓冲就"借"给你了（g_cur_idx 指向它），
 *  驱动那边没有它，**必须**调 capture_release() 还回去，否则队列会越用越少，
 *  4 块用完就再也不出帧（表现为"画面卡住"）。
 * ========================================================================== */
int capture_grab(int fd)
{
    struct pollfd pfd;
    pfd.fd     = fd;
    pfd.events = POLLIN;          /* 等可读（有帧好了） */
    pfd.revents = 0;
    //等待有帧到达，最多 1 秒；返回值 >0 = 有几个 fd 就绪
    int r = poll(&pfd, 1, 1000);    //出错了也是正数没招了

    if (r == 0)  return -2;       /* 超时：1 秒没帧（不是错误，见上面的说明）*/
    if (r < 0) {
        if (errno == EINTR) return -2;   /* 被信号打断，当超时处理（g_running 检查留给上层）*/
        return -1;                       /* poll 本身出错 */
    }

     if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        /* POLLHUP：设备被拔了；POLLERR：设备侧出错（USB 断流、带宽崩溃）。
         * 这三种都只能重连，没法在原地恢复。 */
        LOGE("cap", "设备异常 revents=0x%x", pfd.revents);
        return -1;
    }//掉线了

    struct v4l2_buffer b;
    memset(&b, 0, sizeof(b));
    b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;//视频采集
    b.memory = V4L2_MEMORY_MMAP;

    /* 出队：把"驱动已填好"的那块取出来。回来时 b.index 告诉你取到的是第几块，
     * b.bytesused 是这帧真正有多少字节（MJPEG 是变长的，每帧都不一样）。 */
    if (ioctl(fd, VIDIOC_DQBUF, &b) < 0)
        return -1;

    g_cur_idx  = b.index;
    g_cur_size = b.bytesused;//记录
    return 0;
}

/* 当前帧的起始地址。没借出帧时返回 NULL（调用方要判空）。 */
void *capture_frame_data(void)
{
    if (g_cur_idx < 0) return NULL;
    return g_bufs[g_cur_idx];
}

/* 当前帧的字节数（MJPEG 压缩后的大小，不是 width*height）。 */
size_t capture_frame_size(void)
{
    return g_cur_size;
}

/* 把借出的这块还回驱动队列。没借出时是空操作（幂等，随便调）。 */
void capture_release(int fd)
{
    if (g_cur_idx < 0) return;

    struct v4l2_buffer b;
    memset(&b, 0, sizeof(b));
    b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    b.index  = g_cur_idx;          /* 告诉驱动"还回去的是第几块" */

    if (ioctl(fd, VIDIOC_QBUF, &b) < 0)
        perror("VIDIOC_QBUF(release)");

    g_cur_idx = -1;                /* 标记"没借出"，防止重复还 */
}

/* =====================================================================
 *  停止采集并释放资源
 * =====================================================================
 *  三步的顺序不能换，尤其是第 3 步 —— 换分辨率前必须做，
 *  否则下一次 S_FMT 会 EBUSY（V4L2 规定 buffer 已分配时不许改格式）。
 * ===================================================================== */
void capture_stop(int fd)
{
    if (fd < 0) return;            /* 设备已经关了就直接返回 */

    /* 1. 停流（这一步是解决问题的核心：不停流 munmap 是无效操作）
     *    设备掉了的话 STREAMOFF 会失败，perror 一下继续往下走。 */
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
        perror("VIDIOC_STREAMOFF");

    /* 2. 解除所有 mmap（把用户态地址空间还回去）*/
    for (int i = 0; i < g_buf_count; i++) {
        if (g_bufs[i] && g_bufs[i] != MAP_FAILED)
            munmap(g_bufs[i], g_bufs_len[i]);
        g_bufs[i]     = NULL;
        g_bufs_len[i] = 0;
    }

    g_buf_count = 0;
    g_cur_idx   = -1;
    g_cur_size  = 0;

    /* 3. 把驱动那边的 buffer 也还回去（REQBUFS count=0）
     * ★ 换分辨率必须先做这一步：V4L2 规定 buffer 已分配时
     *   S_FMT 返回 EBUSY。只 STREAMOFF + munmap 不够 ——
     *   munmap 只是解除用户态映射，驱动那边还记着这几块。 */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = 0;                /* count=0 = "我不要缓冲了，收回去" */
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
        perror("[capture] REQBUFS(0)");

    LOGI("cap", "已停止采集");
}

/* =============================================================================
 *  格式协商
 * ========================================================================== */
int capture_negotiate(int fd, unsigned *outW, unsigned *outH, unsigned *outFps)
{
    /* 配置里没有就退回 capture.h 的默认值（1280×720@30） */
    const unsigned reqW   = (unsigned)settings_get_int("capture/width",  CAP_W);
    const unsigned reqH   = (unsigned)settings_get_int("capture/height", CAP_H);
    const unsigned reqFps = (unsigned)settings_get_int("capture/fps",    30);

    if (capture_set_format(fd, reqW, reqH, reqFps) < 0)
        return -1;

    unsigned w = 0, h = 0, fps = 0;
    if (capture_query_format(fd, &w, &h, &fps) < 0)
        return -1;
    if (w == 0 || h == 0)               /* 驱动给了个非法值，当成失败 */
        return -1;

    /* ★ 实际值写回配置 —— 界面读的就是这个文件 */
    settings_set_int("capture/width",  (int)w);
    settings_set_int("capture/height", (int)h);
    if (fps > 0)                        /* 0 = 驱动没报帧率，别把已有的值覆盖成 0 */
        settings_set_int("capture/fps", (int)fps);

    LOGI("cap", "生效格式 %ux%u @%ufps（请求 %ux%u @%ufps）",
         w, h, fps, reqW, reqH, reqFps);

    if (outW)   *outW   = w;
    if (outH)   *outH   = h;
    if (outFps) *outFps = fps;
    return 0;
}
