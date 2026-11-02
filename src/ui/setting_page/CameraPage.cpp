// SPDX-License-Identifier: MIT
/** @file CameraPage.cpp
 *  @brief 摄像头设置页实现
 */
#include "CameraPage.h"
#include "log.h"

#include "app_settings.h"
#include "page_chrome.h"   /* 底部按钮行几何 + 按钮文字反馈（三页共用） */
#include "camera_service.h"  /* ★ 摄像头操作唯一门面 —— UI 不许直接 include platform */
#include "page.h"         /* SCR_W / SCR_H */
#include "ScrollView.h"   /* 可拖动视口（2026-10-31 从本文件拆出去） */
#include "AEValueField.h"  /* 只读数值框 */
#include "AENumberDialog.h" /* 数值修改弹窗 */
#include <cerrno>

#include <QList>
#include <QLabel>
#include <QVariant>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <functional>

#include <linux/videodev2.h>

/* 【这三个 CID 定义在 capture/camera_ctl.h，这里不再重复定义】
 * 同一个常量在两个头文件里各定义一次，是 2026-10-30 那个
 * "白平衡自动写不进去" 的温床 —— 两处的 #ifndef 分支可能算出不同的值。 */

namespace {

/* =============================================================================
 *  10 个数值控制项：亮度 / 对比度 / 饱和度 / 伽马 / 增益 / 色温档位 /
 *  锐度 / 逆光补偿 / 手动曝光值 / （对焦位置——定焦，永久置灰）
 * =============================================================================
 *  【范围和步长不在这里写死】
 *    运行时用 CameraService::ctrlQuery() 问设备 —— 范围是摄像头报的，换一颗就全错。
 *    这张表只管「哪一行放哪一项、写配置用哪个键」。
 *
 *  【对焦位置为什么也在表里】
 *    这块屏是定焦镜头，写进去没反应。放一行置灰的比"凭空少一行"好 ——
 *    用户至少知道有这个功能，只是这台镜头用不了。
 * ========================================================================== */
namespace {

/* 只描述「哪一项摆在哪一格」。
 *
 * CID / 配置键 / 中文名**统一来自 platform/camera 的控制项表**，经
 * `CameraService::ctrlName/ctrlKey` 暴露给 UI —— 那是
 * "哪些控制项要持久化"的唯一真相源：界面保存按它写配置，采集线程启动按它下发。
 * 两边合用一个表，才不会出现"界面写 camera/gain、开机读别的键"这种静默不一致。
 *
 * ★ 下标必须和 platform 控制项表的顺序一一对应（经 CameraService 转发）。 */
struct NumSpec {
    int col, row;
};

const NumSpec kNumSpecs[] = {
    { 0, 3 },   /* [0] 亮度 */
    { 0, 4 },   /* [1] 对比度 */
    { 0, 5 },   /* [2] 饱和度 */
    { 0, 6 },   /* [3] 伽马 */
    { 0, 7 },   /* [4] 增益 */
    { 1, 2 },   /* [5] 色温档位 */
    { 1, 3 },   /* [6] 锐度 */
    { 1, 4 },   /* [7] 逆光补偿 */
    { 1, 6 },   /* [8] 手动曝光值 */
    { 1, 7 },   /* [9] 对焦位置 */
};
constexpr int kNumCount = sizeof(kNumSpecs) / sizeof(kNumSpecs[0]);

} // namespace

/* ---- 版式：两列 × 八行，整块可上下拖动 ----
 *  左列 x=30..456，右列 x=528..980，中间留 72px 当视觉分栏。
 *  右列标签有 5 个汉字的（白平衡自动/抗工频闪烁/手动曝光值），
 *  528+135=663，离控件 700 还有 37px，不会挤在一起。
 *
 *  【为什么改成滚动】
 *    原来 8 行固定排在一屏里，行距只能压到 52（= 下拉框高度，一点余量都没有），
 *    再加项就得动全局版式。现在标题(40..96)和返回钮(524..580)都不动，
 *    中间 96..516 这 420px 交给 ScrollView，行距放到 60（控件之间 8px 呼吸），
 *    内容 8 行 = 480px，超出视口 60px（正好一行），靠拖动看剩下的。
 *    以后加行只改 kRows，这套关系自动成立。
 */
constexpr int kLabelX[2]  = {30, 528};
constexpr int kCtrlX[2]   = {176, 700};
constexpr int kLabelW     = 140;   /* 5 个汉字 × 27px = 135 */
constexpr int kCtrlW      = 280;
constexpr int kComboH     = Theme::Combo::Height;   /* 52 */

constexpr int kViewTop    = 96;                    /* 标题占 40..96 */
constexpr int kViewBottom = 516;                   /* 返回钮 524 起 */
constexpr int kViewH      = kViewBottom - kViewTop;   /* 420 = 7 行 */

/* 【为什么 420 / 60 / 480 这三个数是配套的】
 *   视口正好 7 行、内容 8 行（kRows），于是最大滚动量 = 8*60 - 420 = 60 = **一行**。
 *   行数再多（10 行 → 180 = 3 行）也永远是行距的整数倍，
 *   配合松手吸附（release 时对齐到 60 的倍数），任何时刻每行都是完整可见的，
 *   不会出现"顶部半行 / 底部半行"。改行数只改 kRows，这套关系自动成立。 */
constexpr int kRowStep    = 60;                    /* 52 控件 + 8 呼吸 */
constexpr int kRows       = 8;
constexpr int kBodyH      = kRows * kRowStep;      /* 480 */

int rowY(int row) { return row * kRowStep; }

/* 控件在行内纵向居中：下拉框 52 高正好占满一行，开关/标签 36 高各让 8px */
int widgetY(int row, int h) { return rowY(row) + (kComboH - h) / 2; }

/* 程序固定用 MJPEG 解码（见 SettingsPage 的说明），YUYV 列出来但不可选 */
const char kUsedFourcc[] = "MJPG";

QString noCameraText() { return QString::fromUtf8("无摄像头"); }

} // namespace

CameraPage::CameraPage(QWidget *parent) : AEPage(parent)
{
    setPageTitle(QString::fromUtf8("摄像头设置"));

    /* 所有行都挂在 m_body 上，m_body 在 m_view 里上下移动；标题和返回钮留在这层，
     * 滚动时不动（返回永远在手边，不用先拖回顶）。
     * 注意创建顺序：m_body 必须先于任何控件存在，后面全用 m_body 当父亲。 */
    m_body = new QWidget(this);
    m_body->setGeometry(0, 0, SCR_W, kBodyH);
    /* rowStep = kRowStep：松手吸附到整行（见 ScrollView.h 的说明） */
    m_view = new ScrollView(m_body, this, kRowStep);
    m_view->setGeometry(0, kViewTop, SCR_W, kViewH);

    /* 页面一滚动就把展开的下拉列表收起来：
     * 它的坐标是相对页面算的，页面动了它还留在原地就成了"飘着的框"。 */
    static_cast<ScrollView *>(m_view)->onDragStart = [this]() {
        const QList<AEComboBox *> combos = findChildren<AEComboBox *>();
        for (int i = 0; i < combos.size(); ++i)
            combos.at(i)->dismissPopup();
    };

    /* =========================================================================
     *  行表：8 行 × 2 列 = 16 格，全部占满（kRows = 8）
     * =========================================================================
     *      行   左列（col 0，x=30）        右列（col 1，x=528）
     *      ──────────────────────────────────────────────────────
     *      0    格式      [下拉]           白平衡自动 [开关]
     *      1    分辨率    [下拉]           抗工频闪烁 [下拉]
     *      2    帧率      [下拉]           色温档位   [值 + 修改]
     *      3    亮度      [值 + 修改]      锐度       [值 + 修改]
     *      4    对比度    [值 + 修改]      逆光补偿   [值 + 修改]
     *      5    饱和度    [值 + 修改]      曝光模式   [下拉, 只读]
     *      6    伽马      [值 + 修改]      手动曝光值 [值 + 修改]
     *      7    增益      [值 + 修改]      对焦位置   [值 + 修改, 定焦置灰]
     *
     *  **16 格全部接了控件，没有占位项了**（2026-10-31）：
     *    · 6 个下拉/开关：格式 / 分辨率 / 帧率 / 白平衡自动 / 抗工频闪烁 / 曝光模式
     *    · 10 个「值框 + 修改按钮」：点「修改」开 AENumberDialog 用 ± 档位改；
     *      弹窗点「设置」只更新值框并标记 dirty，**真正下发要等页面底部「保存」**
     *      （见 onSave() 里的遍历下发）。
     *
     *  【左列排的都是"要改就得重建链路"的项】
     *    格式/分辨率/帧率 改完点「保存」才换流；右列除色温外都能热改。
     *    这不是巧合，是按"改了会不会掉帧"分的列。
     * ========================================================================= */

    /* ===================== 左列 ===================== */

    /* ---- 格式 ---- */
    m_lblFormat = addLabel(0, 0, QString::fromUtf8("格式"));
    m_cmbFormat = new AEComboBox(m_body);
    m_cmbFormat->setGeometry(kCtrlX[0], rowY(0), kCtrlW, kComboH);
    connect(m_cmbFormat, &AEComboBox::currentIndexChanged, this, [this](int) {
        if (m_syncing)
            return;
        m_fmtNow = m_cmbFormat->currentData().toString();
        /* 换格式 → 分辨率/帧率两级重建（格式本身不写配置：
         * 现在只有 MJPG 能真跑，写进去等于骗人） */
        m_syncing = true;
        CameraService::ModeInfo all[CameraService::modeMax()];
        const int n = loadModes(all);
        rebuildResolution(all, n);
        rebuildFps(all, n);
        m_syncing = false;
    });

    /* ---- 分辨率 ---- */
    m_lblResolution = addLabel(0, 1, QString::fromUtf8("分辨率"));
    m_cmbResolution = new AEComboBox(m_body);
    m_cmbResolution->setGeometry(kCtrlX[0], rowY(1), kCtrlW, kComboH);
    connect(m_cmbResolution, &AEComboBox::currentIndexChanged, this, [this](int) {
        if (m_syncing)
            return;
        m_syncing = true;
        CameraService::ModeInfo all[CameraService::modeMax()];
        const int n = loadModes(all);
        rebuildFps(all, n);
        m_syncing = false;
        /* 写配置 + 换流都等「保存」，见 onSave() */
    });

    /* ---- 帧率 ---- */
    m_lblFps = addLabel(0, 2, QString::fromUtf8("帧率"));
    m_cmbFps = new AEComboBox(m_body);
    m_cmbFps->setGeometry(kCtrlX[0], rowY(2), kCtrlW, kComboH);
    connect(m_cmbFps, &AEComboBox::currentIndexChanged, this, [this](int) {
        (void)this;      /* 等「保存」一次性提交 */
    });

    /* ---- 以下 5 项：数值型（标签 + 只读数值框 + 修改按钮，控件在 addNumericRow 里建）---- */
    addLabel(0, 3, QString::fromUtf8("亮度"));
    addNumericRow(0, 3, 0);

    addLabel(0, 4, QString::fromUtf8("对比度"));
    addNumericRow(0, 4, 1);

    addLabel(0, 5, QString::fromUtf8("饱和度"));
    addNumericRow(0, 5, 2);

    addLabel(0, 6, QString::fromUtf8("伽马"));
    addNumericRow(0, 6, 3);

    addLabel(0, 7, QString::fromUtf8("增益"));
    addNumericRow(0, 7, 4);

    /* ===================== 右列 ===================== */

    /* ---- 白平衡自动（开关：1 = 自动，0 = 手动） ---- */
    m_lblWbAuto = addLabel(1, 0, QString::fromUtf8("白平衡自动"));
    m_swWbAuto = new ToggleSwitch(m_body);
    m_swWbAuto->setGeometry(kCtrlX[1], widgetY(0, Theme::Toggle::Height),
                            Theme::Toggle::Width, Theme::Toggle::Height);
    connect(m_swWbAuto, &QAbstractButton::toggled, this, [this](bool on) {
        if (m_syncing)
            return;
        (void)on;        /* 用户要求开关按 1/0 写，但写设备要等「保存」*/
    });

    /* ---- 抗工频闪烁（3 档：关闭 / 50Hz / 60Hz） ---- */
    m_lblFlicker = addLabel(1, 1, QString::fromUtf8("抗工频闪烁"));
    m_cmbFlicker = new AEComboBox(m_body);
    m_cmbFlicker->setGeometry(kCtrlX[1], rowY(1), kCtrlW, kComboH);
    m_cmbFlicker->addItem(QString::fromUtf8("关闭"), 0, true);
    m_cmbFlicker->addItem(QString::fromUtf8("50Hz"), 1, true);
    m_cmbFlicker->addItem(QString::fromUtf8("60Hz"), 2, true);
    m_cmbFlicker->setCurrentIndex(1);
    connect(m_cmbFlicker, &AEComboBox::currentIndexChanged, this, [this](int) {
        if (m_syncing)
            return;
        /* 写设备等「保存」 */
    });

    /* ---- 以下 4 项：数值型（同上）---- */
    addLabel(1, 2, QString::fromUtf8("色温档位"));
    addNumericRow(1, 2, 5);

    addLabel(1, 3, QString::fromUtf8("锐度"));
    addNumericRow(1, 3, 6);

    addLabel(1, 4, QString::fromUtf8("逆光补偿"));
    addNumericRow(1, 4, 7);

    /* ---- 曝光模式：设备只报"手动"，固定 1，列出来但不让改 ---- */
    m_lblExpMode = addLabel(1, 5, QString::fromUtf8("曝光模式"));
    m_cmbExpMode = new AEComboBox(m_body);
    m_cmbExpMode->setGeometry(kCtrlX[1], rowY(5), kCtrlW, kComboH);
    m_cmbExpMode->addItem(QString::fromUtf8("手动 (1)"), 1, true);
    m_cmbExpMode->setCurrentIndex(0);
    m_cmbExpMode->setEnabled(false);
    m_lblExpMode->setStyleSheet(Theme::qssTextFlat(Theme::AE::TextMuted));

    /* ---- 以下 2 项：数值型（同上）---- */
    addLabel(1, 6, QString::fromUtf8("手动曝光值"));
    addNumericRow(1, 6, 8);

    addLabel(1, 7, QString::fromUtf8("对焦位置"));
    addNumericRow(1, 7, 9);

    /* ---- 返回：只上报，由状态机决定去哪一页 ---- */
    m_btnBack = PageChrome::makeBackButton(this);
    connect(m_btnBack, &QPushButton::clicked, this, &CameraPage::backRequested);

    /* ---- 保存：和设置页同一个位置（右下角），同样是"写配置 + 立刻生效" ---- */
    m_btnSave = PageChrome::makeSaveButton(this);
    connect(m_btnSave, &QPushButton::clicked, this, &CameraPage::onSave);
}

/* =============================================================================
 *  造行标签（★ 2026-11-01 更正：原标题"造标签 / 占位格子"——占位格子是
 *  视口布局早期的做法，16 格全部接控件后已无占位逻辑，这里只造标签）
 * ========================================================================== */
QLabel *CameraPage::addLabel(int col, int row, const QString &text)
{
    QLabel *l = new QLabel(text, m_body);
    l->setGeometry(kLabelX[col], widgetY(row, 36), kLabelW, 36);
    l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    l->setStyleSheet(Theme::qssTextFlat());
    return l;
}

/* =============================================================================
 *  能力表 → 三个下拉框
 * =============================================================================
 *  数据来源 /etc/dvr/camera.ini（capture_init() 枚举生成），
 *  和设置页读的是同一份，这里不做缓存。
 * ========================================================================== */
/* =============================================================================
 *  PageObject：进场 / 退场
 * =============================================================================
 *  进场做两件事：重读配置（可能刚在设置页保存过）+ 把整屏刷成自己的样子。
 *  退场只有 hide() —— 视频层由 AppController 按 PageLayout 统一恢复。
 * ========================================================================== */
void CameraPage::onEnter()
{
    refresh();      /* 控件状态跟着配置文件走，见 syncDeviceControls() */
    show();
    raise();
    repaint();      /* 同步刷：面板要盖住 DE 视频层，不能等下一帧 */
}

void CameraPage::onLeave()
{
    /* ★ 弹窗是本页的子控件，不 hide 的话切到别的页它还盖在上面 */
    if (m_dlg)
        m_dlg->hide();
    hide();
}

void CameraPage::refresh()
{
    m_syncing = true;

    /* 能力表读一次，三个 rebuild 共用 */
    CameraService::ModeInfo all[CameraService::modeMax()];
    const int n = loadModes(all);

    rebuildFormat(all, n);
    rebuildResolution(all, n);
    rebuildFps(all, n);
    syncDeviceControls();
    refreshNumericValues();   /* 读 10 个数值项的当前值 + 可调性 */

    m_syncing = false;
}

/* 读一次能力表。
 * 返回条数；<=0 表示没插摄像头（或 camera.ini 还没生成）——
 * 各 rebuild 里都有 n <= 0 的降级分支，这里不做判断。 */
int CameraPage::loadModes(CameraService::ModeInfo *all)
{
    return CameraService::modeLoad(all, CameraService::modeMax());
}

void CameraPage::rebuildFormat(const CameraService::ModeInfo *all, int n)
{
    m_cmbFormat->clearItems();

    if (n <= 0) {
        m_cmbFormat->addItem(noCameraText(), QVariant(), false);
        m_cmbFormat->setCurrentIndex(0);
        m_fmtNow.clear();
        muteRow(m_lblFormat, m_cmbFormat, false);
        return;
    }

    QList<QString> fmts;
    for (int i = 0; i < n; ++i) {
        const QString f = QString::fromLatin1(all[i].fourcc);
        if (f.isEmpty() || fmts.contains(f))
            continue;
        fmts.append(f);
    }

    const QString used = QString::fromLatin1(kUsedFourcc);
    if (fmts.contains(used))
        fmts.removeAll(used);
    fmts.prepend(used);          /* MJPG 排第一，且唯一可选 */

    for (int i = 0; i < fmts.size(); ++i)
        m_cmbFormat->addItem(fmts.at(i), fmts.at(i), fmts.at(i) == used);

    m_fmtNow = used;
    m_cmbFormat->setCurrentIndex(0);
    muteRow(m_lblFormat, m_cmbFormat, true);
}

void CameraPage::rebuildResolution(const CameraService::ModeInfo *all, int n)
{
    const QString fmt = m_fmtNow.isEmpty() ? QString::fromLatin1(kUsedFourcc) : m_fmtNow;

    m_cmbResolution->clearItems();

    const int curW = AppSettings::getInt("capture/width",  1280);
    const int curH = AppSettings::getInt("capture/height", 720);
    const QSize cur(curW, curH);

    QList<QSize> sizes;
    for (int i = 0; i < n; ++i) {
        if (QString::fromLatin1(all[i].fourcc) != fmt)
            continue;
        const QSize s((int)all[i].width, (int)all[i].height);
        if (!sizes.contains(s))
            sizes.append(s);
    }

    if (sizes.isEmpty()) {
        m_cmbResolution->addItem(noCameraText(), QVariant(), false);
        m_cmbResolution->setCurrentIndex(0);
        muteRow(m_lblResolution, m_cmbResolution, false);
        return;
    }

    for (int i = 0; i < sizes.size(); ++i)
        m_cmbResolution->addItem(QString("%1 × %2").arg(sizes.at(i).width())
                                     .arg(sizes.at(i).height()), sizes.at(i), true);

    int idx = m_cmbResolution->indexOfData(cur);
    if (idx < 0) {
        /* 配置里的尺寸这个格式不支持 → 追加一项，保证"界面 == 配置"不破 */
        idx = m_cmbResolution->count();
        m_cmbResolution->addItem(QString("%1 × %2").arg(curW).arg(curH), cur, true);
    }
    m_cmbResolution->setCurrentIndex(idx);
    muteRow(m_lblResolution, m_cmbResolution, m_cmbResolution->count() > 1);
}

void CameraPage::rebuildFps(const CameraService::ModeInfo *all, int n)
{
    const QString fmt = m_fmtNow.isEmpty() ? QString::fromLatin1(kUsedFourcc) : m_fmtNow;
    const QSize sel  = m_cmbResolution->currentData().toSize();
    const int curFps = AppSettings::getInt("capture/fps", 30);

    m_cmbFps->clearItems();

    QList<int> fpsList;
    for (int i = 0; i < n; ++i) {
        if (QString::fromLatin1(all[i].fourcc) != fmt)
            continue;
        if ((int)all[i].width != sel.width() || (int)all[i].height != sel.height())
            continue;
        if (all[i].fps == 0)
            continue;
        if (!fpsList.contains((int)all[i].fps))
            fpsList.append((int)all[i].fps);
    }

    if (fpsList.isEmpty()) {
        m_cmbFps->addItem(QString("%1 fps").arg(curFps), curFps, true);
        m_cmbFps->setCurrentIndex(0);
        muteRow(m_lblFps, m_cmbFps, false);
        return;
    }

    for (int i = 0; i < fpsList.size(); ++i)
        m_cmbFps->addItem(QString("%1 fps").arg(fpsList.at(i)), fpsList.at(i), true);

    int idx = m_cmbFps->indexOfData(curFps);
    m_cmbFps->setCurrentIndex(idx >= 0 ? idx : 0);
    muteRow(m_lblFps, m_cmbFps, fpsList.size() > 1);
}

/* =============================================================================
 *  设备当前值 → 界面
 * ==========================================================================
 *  读不到（没插摄像头 / ioctl 失败）就整行置灰，不用默认值去猜。
 * ========================================================================== */
void CameraPage::syncDeviceControls()
{
    int v = 0;

    /* 界面显示的是【配置文件里的值】——它才是"用户保存过的选择"。
     * 采集线程启动时会把它下发给设备，所以正常情况下两者一致；
     * 不一致只可能是设备被别的进程改过，此时打一行日志，界面仍以配置为准
     * （否则用户一进页面看到的值和自己上次保存的不一样，会以为没保存成功）。 */
    const int cfgWb    = AppSettings::getInt("camera/wbAuto", 1);
    const int cfgPower = AppSettings::getInt("camera/powerLine", 1);

    /* 白平衡自动：1 = 自动，0 = 手动 */
    m_swWbAuto->setChecked(cfgWb != 0);
    if (CameraService::getWbAuto(&v)) {
        muteRow(m_lblWbAuto, nullptr, true);
        if ((v != 0) != (cfgWb != 0))
            LOGI("camera", "白平衡自动 设备=%d 配置=%d（以配置显示）", v, cfgWb);
    } else {
        muteRow(m_lblWbAuto, nullptr, false);
    }

    /* 抗工频闪烁：0 关闭 / 1 50Hz / 2 60Hz */
    const int idxCfg = m_cmbFlicker->indexOfData(cfgPower);
    m_cmbFlicker->setCurrentIndex(idxCfg >= 0 ? idxCfg : 1);
    if (CameraService::getPowerLine(&v)) {
        muteRow(m_lblFlicker, m_cmbFlicker, true);
        if (v != cfgPower)
            LOGI("camera", "抗工频闪烁 设备=%d 配置=%d（以配置显示）", v, cfgPower);
    } else {
        muteRow(m_lblFlicker, m_cmbFlicker, false);
    }

    /* 曝光模式：固定手动（1）。读到了就核对一下，读不到也照样显示"手动" */
    if (CameraService::getExposureAuto(&v) && v != 1) {
        LOGW("camera", "注意：曝光模式当前为 %d，界面按手动(1)显示", v);
    }
}


void CameraPage::onSave()
{
    /* ---- 数值控制项：遍历 dirty 的项统一下发 + 写配置 ----
     * 重复写同一个值是幂等的，比漏写一次安全。 */
    /* ---- 数值项下发（亮度/对比度这类，弹窗里改、点「保存」才生效）----
     * 【以亮度为例的完整调用链】
     *   ① 弹窗 AENumberDialog 的 ± 按钮     （只改 m_num[].pendingValue + dirty，
     *                                         不碰设备 —— 「取消」就是真取消）
     *   ② 点「保存」进到这里 →
     *   ③ CameraService::ctrlSet(slot)      [core/camera_service.cpp:65]
     *   ④ camera_ctl_set(cid, value)        [platform/camera/camera_ctl.c:80]
     *   ⑤ ioctl(VIDIOC_S_CTRL)              【硬件立即生效；用的是另开的控制 fd】
     *   ⑥ AppSettings::setInt(key, value)   [settings.ini 落盘]
     *      下次开流成功时 camera_ctl_apply_settings() 会把它读回来重发一遍
     *      （platform/camera/camera_ctl.c），重启程序参数也不丢。
     *
     * ★ 数值项**不走换流**；本函数稍后的 requestFormat 只服务分辨率/帧率。 */
    for (int i = 0; i < m_num.size(); ++i) {
        NumItem &it = m_num[i];
        if (!it.dirty)
            continue;
        if (CameraService::ctrlSet(it.slot, it.pendingValue))
            AppSettings::setInt(CameraService::ctrlKey(i), it.pendingValue);
        it.dirty = false;
    }

    const QSize s = m_cmbResolution->currentData().toSize();
    const int fps = m_cmbFps->currentData().toInt();

    if (!s.isValid() || fps <= 0) {
        LOGW("camera", "保存被忽略：分辨率/帧率无效");
        flashSaved(QString::fromUtf8("无摄像头"));
        return;
    }

    /* ---- ① 写配置文件（和原设置页完全同一条路：capture/width|height|fps）---- */
    AppSettings::setInt("capture/width",  s.width());
    AppSettings::setInt("capture/height", s.height());
    AppSettings::setInt("capture/fps",    fps);

    /* ---- ② 图像控制项也存下来，重启后由采集线程重新下发 ---- */
    const int wbAuto    = m_swWbAuto->isChecked() ? 1 : 0;
    const int powerLine = m_cmbFlicker->currentData().toInt();
    AppSettings::setInt("camera/wbAuto",    wbAuto);
    AppSettings::setInt("camera/powerLine", powerLine);

    /* ---- ③ 立刻生效 ----
     * 换格式只能采集线程自己做（video_fd/解码器/DE 句柄都在它手里），
     * 所以这里只登记请求；控制项是独立的 open/ioctl/close，直接写。 */
    if (!CameraService::requestFormat(s.width(), s.height(), fps))
        LOGE("camera", "换格式请求被拒绝");

    if (!CameraService::setWbAuto(wbAuto))
        LOGE("camera", "写白平衡自动失败");
    if (!CameraService::setPowerLine(powerLine))
        LOGE("camera", "写抗工频闪烁失败");

    LOGI("camera", "已保存：%dx%d @%dfps wbAuto=%d powerLine=%d", s.width(), s.height(), fps, wbAuto, powerLine);

    flashSaved(QString::fromUtf8("已保存"));
}


void CameraPage::flashSaved(const QString &text)
{
    /* 实现收口在 PageChrome，和 SettingsPage 共用同一套 */
    PageChrome::flashButtonText(m_btnSave, text);
}

/* =============================================================================
 *  整行置灰
 * ========================================================================== */
void CameraPage::muteRow(QLabel *label, QWidget *ctrl, bool usable)
{
    if (label)
        label->setStyleSheet(Theme::qssTextFlat(
            usable ? QString() : QString::fromLatin1(Theme::AE::TextMuted)));
    if (ctrl)
        ctrl->setEnabled(usable);
}

/* =============================================================================
 *  数值控制项：10 行「值框 + 修改按钮」
 * =============================================================================
 *  【范围 / 步长 / 可调性全部运行时问设备】
 *    CameraService::ctrlQuery() 给 min/max/可调性；
 *    flags 里的 INACTIVE 表示"此刻不可调"（典型：白平衡自动开着时色温档位）。
 *    这里不做任何硬编码 —— 换一颗摄像头，范围和可调性自动跟着变。
 * ========================================================================== */

/* 建一行：标签已由 addLabel 建好，这里建值框 + 修改按钮 */
void CameraPage::addNumericRow(int col, int row, int slot)
{
    if (slot < 0 || slot >= CameraService::ctrlCount())
        return;

    NumItem it;
    it.slot = slot;
    it.alwaysOff = (slot == 9);   /* 对焦位置：定焦镜头，写进去没反应 */

    it.field = new AEValueField(m_body);
    it.field->setGeometry(kCtrlX[col], widgetY(row, kComboH), 156, kComboH);
    it.field->setPlaceholder(QString::fromUtf8("—"));

    it.btn = new AEButton(QString::fromUtf8("修改"), m_body);
    it.btn->setGeometry(kCtrlX[col] + 180, widgetY(row, kComboH), 100, kComboH);
    const int slotCopy = slot;
    connect(it.btn, &QPushButton::clicked, this,
            [this, slotCopy]() { openNumericDialog(slotCopy); });

    m_num.append(it);
}

/* 打开弹窗。初值 = 【页面上正显示的那个数】。
 *
 * 【为什么不是再读一次 CameraService::ctrlGet】
 *   页面上那个值可能是「弹窗改过、还没保存」的暂存值（dirty）。
 *   如果这里重读硬件，用户会看到：页面写着 8，点开弹窗却是 2 —— 同一个数
 *   在两个地方不一致，比"值旧一点"难理解得多。
 *   （2026-10-31 用户报的就是这个：点击修改后出现的值并不是页面显示的值。）
 *
 *   所以以"眼睛看到的"为准：页面显示什么，弹窗就从什么开始改。
 *   硬件值只在 refreshNumericValues() 里读一次，用来刷新页面。 */
void CameraPage::openNumericDialog(int slot)
{
    if (slot < 0 || slot >= m_num.size())
        return;
    NumItem &it = m_num[slot];
    if (!it.usable)
        return;                    /* 置灰的项不该能走到这里，兜底 */

    const int v = it.field->value();

    if (!m_dlg) {
        m_dlg = new AENumberDialog(this);
        connect(m_dlg, &AENumberDialog::accepted, this, [this](int value) {
            onNumericAccepted(m_dlgSlot, value);
        });
    }

    m_dlgSlot = slot;
    m_dlg->open(QString::fromUtf8(CameraService::ctrlName(slot)), it.min, it.max, v);
}

/* 弹窗点「设置」：只更新值框显示 + 标记 dirty，**不下发到设备**。
 *
 * 【为什么不下发】和格式/分辨率/帧率保持一致 —— 改了停在控件上，
 * 点页面底部的「保存」才统一下发。否则"有些改了立刻生效、有些要保存"
 * 就是两套规则，用户分不清哪些生效了。
 *
 * 【dirty 标志】标记"弹窗里改过但还没保存"。onSave() 遍历所有 dirty 的项
 * 统一下发。用户改了又改回去（值相同）也算 dirty —— 重复写同一个值是幂等的，
 * 比漏写一次安全。 */
void CameraPage::onNumericAccepted(int slot, int value)
{
    if (slot < 0 || slot >= m_num.size())
        return;
    NumItem &it = m_num[slot];

    it.dirty = true;
    it.pendingValue = value;
    it.field->setValue(value);
    /* ★ 不在这里 CameraService::ctrlSet —— 用户要求：点「保存」才统一下发 */
}

/* 刷新 10 行：读范围 + 当前值 + 可调性 */
void CameraPage::refreshNumericValues()
{
    for (int i = 0; i < m_num.size(); ++i) {
        NumItem &it = m_num[i];

        CameraService::CtrlRange r;
        int v = 0;

        const bool q = CameraService::ctrlQuery(it.slot, &r);
        const bool g = CameraService::ctrlGet(it.slot, &v);

        it.min = r.lo;
        it.max = r.hi;

        /* 可调 = 查得到 + 设备没标 INACTIVE + 不是定焦这种"永远没用"的 */
        it.usable = q && g && !r.inactive && !it.alwaysOff;

        if (!q) {
            it.field->setPlaceholder(QString::fromUtf8("—"));   /* 设备不认识这项 */
        } else if (g) {
            it.field->setValue(v);
        }

        it.field->setUsable(it.usable);
        it.btn->setEnabled(it.usable);
    }
}

