// SPDX-License-Identifier: MIT
/** @file app_controller.cpp
 *  @brief 编排层实现：页面切换 / 视频层布局 / 录制接线（接线在构造函数里）
 */
#include "app_controller.h"
#include "log.h"
#include "app_state.h"
#include "MainUi.h"
#include "SettingsPage.h"
#include "StoragePage.h"
#include "CameraPage.h"
#include "page_object.h"
#include "RecordController.h"  /* 录制接线（构造函数里 connect） */
#include "display.h"          /* display_set_visible / display_set_layout */

#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <cstdio>

/* =============================================================================
 *  页面布局意图 → DE 实际参数（crop / win 八个数字）
 * =============================================================================
 *  【输入】page.cpp 里这一页的 PageLayout（设计基准 1280×720 下的意图值）
 *          + 采集协商出的实际源尺寸 srcW × srcH
 *  【输出】DisplayLayout：「从源图裁哪块（crop）→ 铺到屏幕哪块（win）」。
 *          display 层不认识"页面"，只认这八个数字。
 *
 *  【为什么不是纯字段搬运】DE 不做缩放，crop 的宽高比必须等于 win 的宽高比，
 *  否则画面被拉伸；而源尺寸一变比例就变 —— 所以下面有三步计算
 *  （高度等比 → 宽度跟窗口比 → 装不下就取满宽并垂直居中），详见函数体内注释。
 * ========================================================================== */
static DisplayLayout toDisplayLayout(Page p, int srcW, int srcH)
{
    const PageLayout &L = pageLayout(p);

    /* -----------------------------------------------------------------
     *  DE 只做「从源图裁一块 → 铺到屏幕一块」的直接映射，【不做等比缩放】。
     *  所以 cropW/cropH 的比例必须等于 winW/winH，否则画面被拉伸。
     *
     *  page.cpp 的 crop 是按 PAGE_REF_W × PAGE_REF_H（1280×720）写的，
     *  采集分辨率一变，直接按分辨率等比放大会出错：
     *     640×480 是 4:3，按比例缩出来的 crop(0,40,640,400) 是 1.6:1，
     *     塞进 2.133:1 的窗口 → 画面横向压扁。
     *
     *  正确做法分三步：
     *    ① 高度：按页面意图（L.cropH 占基准高度的比例）等比缩放
     *    ② 宽度：由【窗口】宽高比反推，不是由源图反推
     *    ③ 宽度超过源宽（源图比窗口更"方"）就反过来取满宽、
     *       高度按窗口比缩回来、垂直居中
     *
     *  验证：这套规则在 1280×720 下能还原出 page.cpp 的两套预设
     *      Main        1024/480 → crop(0,60,1280,600)   ✓ 与 kMain 一致
     *      Fullscreen  1024/600 → crop(26,0,1228,720)   ✓ 与 kFullscreen 一致
     *  换分辨率后：
     *      640×480    → crop(0,90, 640,300)
     *      1920×1080 → crop(0,90,1920,900)
     * ----------------------------------------------------------------- */
    const int sx = (srcW > 0) ? srcW : PAGE_REF_W;
    const int sy = (srcH > 0) ? srcH : PAGE_REF_H;

    /* ① 高度：这一页要用源图高度的百分之多少 */
    int cropH = L.cropH * sy / PAGE_REF_H;

    /* ② 宽度：跟着窗口比例走 */
    int cropW = (L.winH > 0) ? (cropH * L.winW / L.winH) : sx;

    /* ③ 装不下就取满宽，高度反过来算，垂直居中 */
    int cropY;
    if (cropW > sx) {
        cropW = sx;
        cropH = (L.winW > 0) ? (sx * L.winH / L.winW) : sy;
        cropY = (sy - cropH) / 2;
    } else {
        cropY = L.cropY * sy / PAGE_REF_H;
    }

    /* ④ 偶数对齐：NV12 的 UV 平面是半尺寸，奇数宽高在 DE 上容易出兼容问题 */
    cropW &= ~1;
    cropH &= ~1;
    if (cropY & 1) cropY--;
    int cropX = (sx - cropW) / 2;
    if (cropX & 1) cropX--;

    DisplayLayout d;
    d.cropX = cropX;  d.cropY = cropY;
    d.cropW = cropW;  d.cropH = cropH;
    d.winX  = L.winX; d.winY  = L.winY;
    d.winW  = L.winW; d.winH  = L.winH;
    return d;
}

AppController::AppController(AppState *state, MainUi *mainUi,
                             SettingsPage *settings, CameraPage *camera,
                             StoragePage *storage, RecordController *recorder,
                             QObject *parent)
    : QObject(parent)
    , m_state(state)
    , m_mainUi(mainUi)
    , m_settings(settings)
    , m_camera(camera)
    , m_storage(storage)
    , m_recorder(recorder)
{
    /* ==================================================================
     *  ① 注册页面对象
     * ==================================================================
     *  加新页面就在这里加一行 —— applyPage() 不用动。
     *
     *  MainUi / SettingsPage / CameraPage 自己就实现了 PageObject：
     *    主界面   onEnter = 顶栏底栏亮出来
     *    设置页   onEnter = show + raise + repaint
     *    摄像头页 onEnter = refresh + show + raise + repaint
     *  所以注册表里都是 PageObject*，控制器不需要为主界面做特殊处理。 */
    m_pageObjects.insert(Page::Main,           m_mainUi);
    m_pageObjects.insert(Page::Settings,       m_settings);
    m_pageObjects.insert(Page::CameraSettings, m_camera);
    m_pageObjects.insert(Page::Storage,        m_storage);

    /* ==================================================================
     *  ② 录制接线：底栏按钮 → RecordController → 界面回写
     * ==================================================================
     *  【为什么在编排层，不在 main.cpp】main 只做装配（创建对象、设字体、装信号），
     *  "哪个按钮连到哪个业务"属于业务知识，必须收在编排层。
     *
     *  【为什么不进 AppState】录制和页面正交：录像中可以进设置页。
     *  塞进页面状态机只会让两边都变复杂。 */
    QObject::connect(&m_mainUi->bottomBar(), &BottomBar::recordClicked,
                     m_recorder, &RecordController::toggle);

    /* 第三个参数是 context（必须是 QObject）—— MainUi 不是 QObject，
     * 所以这里用 recorder 自己作 context：lambda 在 recorder 所在线程
     * （主线程）执行，且随 recorder 一起失效，不会出现悬空回调。 */
    QObject::connect(m_recorder, &RecordController::stateChanged, m_recorder,
                     [this](RecState r) {
                         m_mainUi->setRecording(r == RecState::Recording);
                     });

    /* ==================================================================
     *  ② 接线：UI 事件 → 页面状态机（只接【页面事件】）
     * ================================================================== */
    connect(&m_mainUi->bottomBar(), &BottomBar::settingsClicked, m_state,
            [this] { m_state->handleEvent(Event::ClickSettings); });

    /* 底栏「存储」→ 存储页。
     * 2026-10-31 之前这个位置是「回放」（playbackClicked / ClickPlayback，都没接线）；
     * 改成「存储」之后把整条链补齐：按钮 → 信号 → 事件 → 转移表 → 页面。 */
    connect(&m_mainUi->bottomBar(), &BottomBar::storageClicked, m_state,
            [this] { m_state->handleEvent(Event::ClickStorage); });

    connect(m_settings, &SettingsPage::backRequested, m_state,
            [this] { m_state->handleEvent(Event::ClickBack); });

    /* 设置页 ⇄ 摄像头设置页：两个平级页面，各自只发一个信号 */
    connect(m_settings, &SettingsPage::cameraRequested, m_state,
            [this] { m_state->handleEvent(Event::ClickCamera); });

    connect(m_camera, &CameraPage::backRequested, m_state,
            [this] { m_state->handleEvent(Event::ClickBack); });

    /* 存储页「返回」—— 和设置页/相机页同一个事件。
     * ★ 2026-10-31 补：新建页面时漏了这一步，kPageTable 里虽然写了
     *   { Storage, ClickBack, Main }，但没人把信号送进状态机，
     *   结果进了存储页点「返回」出不来。 */
    connect(m_storage, &StoragePage::backRequested, m_state,
            [this] { m_state->handleEvent(Event::ClickBack); });

    /* 录制接线在构造函数 ② 段 —— 别在这里重复连。 */

    /* ==================================================================
     *  ③ 页面变了 → 按新页面调度
     * ================================================================== */
    connect(m_state, &AppState::pageChanged,
            this, &AppController::onPageChanged);

    /* ---- 源尺寸看门狗（200ms 一拍，常开不停）----
     * 【每一拍做什么】onSourceReady()：读 display 当前源宽高，
     *   · 和上次应用的值不一样  → 重新 applyPage()：按新源尺寸重算 crop/win
     *   · 有挂起的页面（m_hasPending）→ 补上那次调度
     *   · 都没有 → 什么都不做，等下一拍
     * 【为什么用轮询，而不是"采集线程通知我"】采集线程不能碰 Qt 对象，
     *   跨线程通知要加队列和锁；而源尺寸一分钟变不了几次，200ms 轮询最简单、代价可忽略。
     * 【它防止什么】换分辨率后采集线程只重建解码器，不会算新 crop ——
     *   没有这拍轮询，画面就会用旧 crop 去切新尺寸（拉伸/错位）。 */
    m_waitSrc = new QTimer(this);
    connect(m_waitSrc, &QTimer::timeout, this, &AppController::onSourceReady);

    /* 常开：每拍自检，没变化就返回。"每一拍做什么"见上面创建处的注释。 */
    m_waitSrc->start(200);
}

/* =============================================================================
 *  启动：先让主界面亮出来，250ms 后做完整布局
 * ========================================================================== */
void AppController::start()
{
    /* 等价于"切到 Main 页的进场动作"。必须在 exec() 之前调，
     * Qt 才会把顶栏底栏排进首帧绘制，否则 250ms 内屏幕是空的。 */
    m_mainUi->onEnter();
    m_currentPage = m_mainUi;

    /* 250ms 后才做完整布局。
     * 原因：Qt 起来后会先整屏画一次（linuxfb 的 doRedraw 会先把脏区
     * fillRect 成不透明）。如果立刻清视频区，紧接着就被这次重绘盖掉。 */
    QTimer::singleShot(250, this, &AppController::bootApply);
}

void AppController::bootApply()
{
    m_booted = true;
    applyPage(m_state->page());
}

/* =============================================================================
 *  源尺寸就绪后，把挂起的页面调度补上
 * =============================================================================
 *  applyPage() 里 onEnter / onLeave / sendPostedEvents 都是幂等的，
 *  重跑一遍不会有副作用。
 * ========================================================================== */
void AppController::onSourceReady()
{
    const int w = display_src_w();
    const int h = display_src_h();
    if (w <= 0 || h <= 0)
        return;                       /* 还没好，下一拍再试 */

    /* bootApply 还没跑就先不动 —— 保持"Qt 首帧画完之后再布局"的原始时序 */
    if (!m_booted)
        return;

    const bool changed = (w != m_appliedSrcW || h != m_appliedSrcH);
    if (!changed && !m_hasPending)
        return;

    if (changed) {
        LOGI("ctrl", "源尺寸 %dx%d（原 %dx%d），重算布局", w, h, m_appliedSrcW, m_appliedSrcH);
        m_appliedSrcW = w;
        m_appliedSrcH = h;
    }

    m_hasPending = false;
    /* 用当前页，不是 m_pendingPage —— 用户可能已经切到别的页了 */
    applyPage(m_state->page());
}

void AppController::onPageChanged(Page from, Page to)
{
    Q_UNUSED(from)
    applyPage(to);
}

/* =============================================================================
 *  按页面调度
 * =============================================================================
 *  四步，顺序有硬约束：
 *
 *    ① 新页面上场     —— 只标脏（show/raise/repaint 都不会立刻上屏）
 *    ② 旧页面退场     —— 只标脏
 *    ③ 同步 flush     —— 把①②的脏区立刻合成并写进 fb0
 *    ④ 视频层         —— UI 已经落地之后，才切 DE 图层
 *
 *  【为什么 ③ 必须在 ④ 之前】
 *    repaint()/show()/hide() 在这条链上只做两件事：把像素画进窗口自己的
 *    backing store，然后 platformScreen()->setDirty() 投递一个
 *    QEvent::UpdateRequest。真正把 2.4MB 写进 /dev/fb0 的是
 *    QFbScreen::doRedraw()，发生在下一次事件循环。
 *
 *    而 display_set_visible() 是同步 ioctl，DE 下一场就生效。
 *    中间不插同步 flush 的话，进设置页会出现：视频层已关、fb0 还是旧主界面
 *    画面 → 视频区黑一到三帧；返回时同理反向。
 *
 *  【为什么用 sendPostedEvents 而不是 processEvents】
 *    只派发 UpdateRequest 一类事件，不跑定时器 / 网络 / 用户输入，
 *    避免 processEvents 那种"在回调里重入任意逻辑"的风险。
 *
 *  【为什么不再清 fb0】
 *    linuxfb 插件改成 ARGB32 之后，doRedraw 合成脏区时填的是**透明**而不是黑
 *    （hasAlphaChannel() ? transparent : black 走透明分支），
 *    hide() 造成的重绘本身就把视频区擦干净了。再 pwrite 一遍反而成了
 *    第二个写者：它同步完成，而 Qt 的重绘还排在队列里 →
 *    视频区先翻、顶底栏后翻，肉眼就是"刷两次"。
 *    详见 参考资料/项目记录/07-Qt透明度改造验证.md 第十一节。
 * ========================================================================== */
void AppController::applyPage(Page p)
{
    const PageLayout &L = pageLayout(p);

    /* ---- ① 新页面上场（只标脏，不上屏）---- */
    PageObject *next = m_pageObjects.value(p, nullptr);
    if (next && next != m_currentPage)
        next->onEnter();

    /* ---- ② 旧页面退场（同样只标脏）---- */
    if (m_currentPage && m_currentPage != next)
        m_currentPage->onLeave();

    m_currentPage = next;

    /* ---- ③ 立刻把脏区压进 fb0：屏幕先变成新页面的完整状态 ----
     * 只派发 UpdateRequest，不碰定时器 / 输入 / 网络。
     * 少了这一步，下面切视频层时 fb0 还是旧画面 → 黑一到三帧。 */
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);

    /* ---- ④ 视频层：UI 落地之后才切 ---- */
    const int srcW = display_src_w();
    const int srcH = display_src_h();

    if (L.videoVisible && (srcW <= 0 || srcH <= 0)) {
        /* 采集线程还没跑完 capture_query_format + display_init，
         * 源尺寸未知 → 算不出正确的 crop。
         *
         * 这时【不能先把视频层打开】：display_init 里那套 crop 是按
         * 基准尺寸算的，分辨率一变就越界。
         * g_visible 保持 0，屏幕上不会出现错位画面，
         * 等源尺寸就绪 onSourceReady() 会把这次调度补上。 */
        m_pendingPage = p;
        m_hasPending  = true;
        if (m_waitSrc && !m_waitSrc->isActive())
            m_waitSrc->start(200);
        LOGW("ctrl", "源尺寸未就绪，挂起 %s 的视频层", pageName(p));
        return;
    }

    /* 记下已应用的源尺寸，避免 onSourceReady() 下一拍又重复应用一次 */
    m_appliedSrcW = srcW;
    m_appliedSrcH = srcH;

    DisplayLayout dl = toDisplayLayout(p, srcW, srcH);
    if (L.videoVisible) {
        display_set_layout(&dl);
        display_set_visible(1);
    } else {
        display_set_visible(0);
    }

    LOGI("ctrl", "布局完成：%s", pageName(p));
}
