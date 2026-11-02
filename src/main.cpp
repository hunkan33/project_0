// SPDX-License-Identifier: MIT
/** @file main.cpp
 *  @brief 程序入口：QApplication、全局字体、页面装配、采集线程启动
 */
#include <QApplication>

#include "log.h"
#include "ui_probe.h"   /* 测试用：进程内点击注入，DVR_UI_PROBE=1 才生效 */
#include <QFont>
#include <QTimer>
#include <cstdio>

#include "MainUi.h"
#include "SettingsPage.h"
#include "CameraPage.h"
#include "StoragePage.h"
#include "RecordController.h"   /* main 只创建对象；接线在 AppController */
#include "app_state.h"
#include "app_controller.h"
#include "app_settings.h"
#include "capture_thread.h"
#include <pthread.h>
#include <signal.h>

/* 全局退出标志：信号置 false → 退出泵让 exec() 返回 → 采集线程看到后自己收尾 */
volatile bool g_running = true;

/** @brief SIGTERM / SIGINT 处理器：只置标志，**绝对不要在这里碰 Qt**。
 *
 *  @note 信号处理函数不是异步信号安全上下文，这里连 printf 都不能调；
 *        真正的退出由主循环里的"退出泵"完成（见 main() 里那个 QTimer）。
 *  @warning **安装位置比函数体重要**：必须放在 `QApplication` 构造**之后**。
 *           linuxfb 插件（`libqlinuxfb.so`）会在 `QApplication` 构造期间创建
 *           `QFbVtHandler`，用 sigaction 一次性接管 SIGINT / SIGTSTP / SIGCONT / **SIGTERM**，
 *           收到 TERM 的做法是「恢复虚拟终端 → `_exit(1)`」—— 不链旧 handler、
 *           不跑 atexit/析构。我们先装就会被它覆盖。
 *  @note 2026-11-01 实测：200ms 我们装上、250ms 被插件覆盖，
 *        之后 `kill -TERM` → 进程 500ms 内 `exit(1)`、日志一行不增。详见 AGENT.md P13。
 */
static void on_term(int) { g_running = false; }

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* 日志尽早初始化：之后所有模块的 LOGx 才有单调时间戳。
     * 放在 QApplication 之前 —— Qt 自己也可能打日志，我们要先拿到锚点。 */
    log_init(LOG_LEVEL_INFO);

    QApplication app(argc, argv);

    /* ---- 退出信号：**必须**放在 QApplication 之后，不能提前 ----
     * linuxfb 插件（libqlinuxfb.so）在 QApplication 构造期间会创建 QFbVtHandler，
     * 它用 sigaction 接管 SIGINT(2)/SIGTSTP(20)/SIGCONT(18)/SIGTERM(15)，
     * 收到 TERM 的做法是：恢复虚拟终端 → setTTYCursor(true) → **`_exit(1)`**。
     * 注意那是 _exit：连 atexit 都跳过，Qt 主循环不会返回，
     * decoder_destroy / capture_stop / close(fd) 一个都不会被调到。
     *
     * @note 代价是失去插件的 VT 恢复（VT_RELDISP/VT_ACTIVATE + 光标复位）。
     *       本机不跑 getty、不做 VT 切换，没有影响。
     * @warning 如果哪天把这两行又挪回 main() 开头，A1 那个坑会原样复现，
     *          而现象是"看起来正常停止、其实什么都没收尾"—— 极难发现。 */
    signal(SIGTERM, on_term);
    signal(SIGINT,  on_term);

    /* 测试探针：只有 DVR_UI_PROBE=1 时才启动，其余情况立即返回 */
    ui_probe_start();

    /* ---- 全局字体：思源黑体 CN 27px（2026-10-30 统一，2026-10-31 补回）----
     * */
    {
        QFont appFont(QStringLiteral("Source Han Sans CN"));
        appFont.setPixelSize(27);
        QApplication::setFont(appFont);
    }

    /* ---- 配置：必须在创建任何 UI 之前 ----
     * 这里晚一步，控件就会先显示默认值、下一帧再跳成文件里的值。 */
    AppSettings::load();

    MainUi           mainUi;      /* 顶栏 + 底栏，作为一个对象管理 */
    SettingsPage     settings;    /* 设置页（全屏遮挡页） */
    CameraPage       camera;      /* 摄像头设置页（同样全屏遮挡，与设置页平级） */
    StoragePage      storage;     /* 存储页（同上，底栏「存储」进入） */
    AppState         state;       /* 页面状态机：只管"在哪一页" */
    RecordController recorder;    /* 录制业务：和"在哪一页"无关 */

    /*  页面 + 录制接线：全在 AppController 里
     */
    AppController ctrl(&state, &mainUi, &settings, &camera, &storage, &recorder);

    /* ---- 启动：显示窗口 + 250ms 后首次清屏布局 ---- */
    ctrl.start();

    /* ---- 启动采集线程（阻塞活都在它里面）---- */
    pthread_t tid;
    pthread_create(&tid, NULL, capture_thread, NULL);

    /* ---- 退出泵：把 g_running=false 翻译成 Qt 的退出 ----
     */
    QTimer quitWatch;
    QObject::connect(&quitWatch, &QTimer::timeout, []() {
        if (!g_running) {
            LOGI("ctrl", "收到退出信号，开始收尾");
            QCoreApplication::quit();
        }
    });
    quitWatch.start(100);

    int ret = app.exec();

    /* exec() 返回后才置 false 是刻意的：让采集线程活到 Qt 拆窗口为止，
     * 否则退出过程中 DE 视频层可能刷在已销毁的窗口上。 */
    g_running = false;
    pthread_join(tid, NULL);
    LOGI("ctrl", "已退出（rc=%d）", ret);
    return ret;
}

