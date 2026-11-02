// SPDX-License-Identifier: MIT
/** @file SettingsPage.cpp
 *  @brief 系统设置页实现
 */
#include "SettingsPage.h"
#include "log.h"
#include "app_settings.h"
#include "page_chrome.h"   /* 底部按钮行几何 + 按钮文字反馈（三页共用） */


namespace {

/* ---- 版式：左标签 + 右控件 ----
 * 标签列 x=60 宽 272（"卡满时覆盖最旧的片段" 10 个汉字 × 27px = 270，正好放下；
 * 到控件列 x=340 还差 8px，不能再窄）
 */
constexpr int kLabelX = 60;
/* 【标签列宽按 27px 字体算】
 * 最长的标签是「卡满时覆盖最旧的片段」10 个汉字，思源汉字是全角 = 字号宽，
 * 10 × 27 = 270px。留 10px 余量 → 280。
 * 2026-10-30 之前这里是 272（按 24px 的 240 留余量算的），
 * 全局字号统一到 27px 后文字刚好顶满、离控件只剩 8px，视觉上贴死了。 */
constexpr int kLabelW = 280;
constexpr int kLabelH = 36;
/* 控件列左移量：60 + 280 + 20（间隙）= 360 */
constexpr int kCtrlX  = 360;

/* 两行：上电自动开始录像 / 卡满时覆盖最旧的片段。
 * 摄像头那三行搬走后，剩下的两行放中间偏上（220 / 300），不贴着标题也不压着按钮。 */
constexpr int kRowAutoY = 220;
constexpr int kRowLoopY = 300;

/* 行中心相对行顶的偏移。开关 36 高、标签 36 高，都按这个中心对齐。 */
constexpr int kCenterOffset = 26;

int centerTop(int rowY, int h) { return rowY + kCenterOffset - h / 2; }

} // namespace

SettingsPage::SettingsPage(QWidget *parent) : AEPage(parent)
{
    setPageTitle(QString::fromUtf8("设置"));

    /* =========================================================================
     *  第 1 行：上电自动开始录像
     * ====================================================================== */
    m_lblAutoStart = makeLabel(kRowAutoY, QString::fromUtf8("上电自动开始录像"));

    m_swAutoStart = new ToggleSwitch(this);
    m_swAutoStart->move(kCtrlX, centerTop(kRowAutoY, Theme::Toggle::Height));

    /* =========================================================================
     *  第 2 行：卡满覆盖最旧片段
     * ====================================================================== */
    m_lblLoopOverwrite = makeLabel(kRowLoopY, QString::fromUtf8("卡满时覆盖最旧的片段"));

    m_swLoopOverwrite = new ToggleSwitch(this);
    m_swLoopOverwrite->move(kCtrlX, centerTop(kRowLoopY, Theme::Toggle::Height));

    /* =========================================================================
     *  先接信号，再用文件里的值初始化
     * =========================================================================
     *  先接信号 → setChecked 会触发一次 toggled → setBool 写一遍。
     *  这一遍通常是空写（值相同，setString 里会提前 return），
     *  但若文件里的值和默认值不同，正好借这次把文件写成自洽状态。
     * ====================================================================== */
    connect(m_swAutoStart, &QAbstractButton::toggled, this, [](bool on) {
        AppSettings::setBool("record/autoStart", on);
    });
    connect(m_swLoopOverwrite, &QAbstractButton::toggled, this, [](bool on) {
        AppSettings::setBool("record/loopOverwrite", on);
    });

    m_swAutoStart->setChecked(AppSettings::getBool("record/autoStart", true));
    m_swLoopOverwrite->setChecked(AppSettings::getBool("record/loopOverwrite", true));

    /* =========================================================================
     *  说明文字：这两项是预留开关
     * =========================================================================
     *  【为什么必须有这一行】
     *    `record/autoStart` 和 `record/loopOverwrite` 目前**全工程没有消费者**
     *    （录像模块还没落地，只有本页读写这两个键）。
     *    也就是说：拨了、也保存了，但不会有任何行为变化。
     *    不写清这一点，用户会以为"我开了自动录像" —— 那是最糟的假象。
     *
     *  【什么时候删】
     *    录像落盘接通、`RecordController` 真的读这两个键之后，删掉这行。
     *    ★ 删之前先 grep 确认：录制侧确实读了它们。
     * ====================================================================== */
    m_lblNote = new QLabel(QString::fromUtf8("以上两项为预留开关，当前版本不影响录像"), this);
    m_lblNote->setGeometry(kLabelX, 380, SCR_W - 2 * kLabelX, 36);
    m_lblNote->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lblNote->setStyleSheet(Theme::qssTextFlat(Theme::AE::TextMuted));

    /* =========================================================================
     *  底部按钮行：y = 524，三页统一
     * =========================================================================
     *  【为什么是 524 不是 512】
     *    CameraPage 有个 7 行视口占 96..516，按钮必须在 516 之下，
     *    留 8px 间距 → 524。设置页本来没有这个约束（原来用 512），
     *    但两页之间来回切时按钮会上下跳 12px，很扎眼 —— 所以设置页跟着 524。
     *    底边距因此是 600-524-56 = 20px，和底栏按钮的 8px 同一量级，不突兀。
     *
     *  【以后加页面的按钮行，也用 524】
     * ====================================================================== */

    /* 返回按钮 —— 只上报事件，不自己改状态 */
    m_btnBack = PageChrome::makeBackButton(this);
    connect(m_btnBack, &QPushButton::clicked, this, &SettingsPage::backRequested);

    /* 保存按钮 —— 右下角。开关本来就是点亮即写盘，这里再显式提交一次，
     * 一是给用户明确的"已保存"反馈，二是不让按钮变成摆设。 */
    m_btnSave = PageChrome::makeSaveButton(this);
    connect(m_btnSave, &QPushButton::clicked, this, &SettingsPage::onSave);

    /* 摄像头设置入口 —— 两钮之间居中。只发信号，切页交给状态机 */
    m_btnCamera = PageChrome::makeCenterButton(QString::fromUtf8("摄像头设置"), this);
    connect(m_btnCamera, &QPushButton::clicked, this, &SettingsPage::onOpenCamera);
}

/* =============================================================================
 *  造一个行标签：位置、对齐、配色统一在这里，调用方只给行号和文字
 * ========================================================================== */
QLabel *SettingsPage::makeLabel(int rowY, const QString &text)
{
    QLabel *l = new QLabel(text, this);
    l->setGeometry(kLabelX, centerTop(rowY, kLabelH), kLabelW, kLabelH);
    l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    l->setStyleSheet(Theme::qssTextFlat());
    return l;
}

/* =============================================================================
 *  「保存」
 * =============================================================================
 *  摄像头相关的保存已经搬到 CameraPage::onSave()（那边写 capture/ 和 camera/ 开头的键）。
 *  这里只剩记录相关的两个开关。
 *
 *  【两个开关是"点亮即写盘"】
 *    toggled → setBool，拨动那一刻就已经进了 `/etc/dvr/settings.ini`。
 *    所以这个按钮实质是**幂等提交**：再写一遍同样的值（值相同会被 setString 提前 return）。
 *
 *  【★ 但"保存成功"不等于"生效"】
 *    这两个键目前没有任何消费者（见页面上那行说明）。所以：
 *      · 提示语只说「已保存」，**不许**说「已生效」或「已开启自动录像」
 *      · 等录像接通、RecordController 真的读它们之后，才谈得上"生效"
 *
 *  【为什么要有反馈】
 *    早先这里只有一行 LOGI，界面零变化 —— 用户以为没点上，会连点三次。
 *    现在改成按钮文字反馈（与 CameraPage::flashSaved 同一套）。
 * ========================================================================== */
void SettingsPage::onSave()
{
    const bool autoStart = m_swAutoStart->isChecked();
    const bool loopOver   = m_swLoopOverwrite->isChecked();

    /* 落盘结果如实返回（setBool 现在返回 bool）。
     * 以前它是 void，所以无论磁盘满/只读，界面都只能无条件说"已保存" —— 那是假的。
     * 注意用 `&` 而不是 `&&`：两个键都要尝试写，不能因为第一个失败就跳过第二个。 */
    const bool w1 = AppSettings::setBool("record/autoStart",    autoStart);
    const bool w2 = AppSettings::setBool("record/loopOverwrite", loopOver);
    const bool ok = w1 && w2;

    LOGI("set", "保存：autoStart=%d loopOverwrite=%d 回读=%s",
         (int)autoStart, (int)loopOver, ok ? "一致" : "不一致");

    flashSaved(ok ? QString::fromUtf8("已保存") : QString::fromUtf8("保存失败"));
}

/* =============================================================================
 *  按钮文字反馈：显示 800ms 后恢复「保存」
 * =============================================================================
 *  与 CameraPage::flashSaved() 保持同一套（同一时长、同一位置、同一恢复文案），
 *  两页点了保存的观感才一致。
 * ========================================================================== */
void SettingsPage::flashSaved(const QString &text)
{
    /* 实现收口在 PageChrome，和 CameraPage 共用同一套（时长、复位文案都只有一份） */
    PageChrome::flashButtonText(m_btnSave, text);
}

/* =============================================================================
 *  「摄像头设置」入口
 * =============================================================================
 *  只发一个信号。切页是状态机的事：
 *      Settings --ClickCamera--> CameraSettings
 *  本页的 hide() 由 AppController::applyPage() 在"旧页面退场"那一步调，
 *  顺序保证新页面先盖住再收旧的，中间不会闪出黑底。
 *
 *  【以前不是这样】
 *  早先这里是 hide() 自己 + show() 摄像头页，等于绕开状态机手工切页，
 *  视频层开关和清屏时序都得在这儿另补一遍。现在统一了。
 * ========================================================================== */
void SettingsPage::onOpenCamera()
{
    emit cameraRequested();
}

SettingsPage::~SettingsPage() = default;

/* =============================================================================
 *  进场：把面板亮出来，并同步绘制
 * =============================================================================
 *  【为什么必须 repaint()】
 *    这个面板要盖住下面的 DE 视频层。show() 只是把它标记为可见，
 *    真正的绘制要等事件循环；而 repaint() 是同步的，返回时像素已经写进 fb0。
 *    遮视频这件事不能等一帧。
 * ========================================================================== */
void SettingsPage::onEnter()
{
    /* 每次进设置页都重读配置文件 —— 配置是唯一真相，
     * 别的地方（将来的迁移工具、手改文件）也可能改过这两个开关。 */
    m_swAutoStart->setChecked(AppSettings::getBool("record/autoStart", true));
    m_swLoopOverwrite->setChecked(AppSettings::getBool("record/loopOverwrite", true));

    show();
    raise();
    repaint();
}

/* =============================================================================
 *  退场：hide()
 * =============================================================================
 *  hide() 会让 Qt 把这块区域标记为脏、随后重绘。
 *  linuxfb 插件是 ARGB32，重绘时填的是**透明**而不是黑，所以：
 *    · 视频区会被擦干净 → DE 的 ch[0] 直接透出来，不需要谁来清 fb0；
 *    · 也不再有"一帧黑"（那是 RGB32 时代的问题，见 07 文档）。
 *  这里只管显隐；视频层开关由 AppController 按 PageLayout 统一处理。
 * ========================================================================== */
void SettingsPage::onLeave()
{
    hide();
}
