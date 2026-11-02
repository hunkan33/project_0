// SPDX-License-Identifier: MIT
/** @file AENumberDialog.cpp
 *  @brief 数值弹窗实现（含手绘返回符号）
 */
#include "AENumberDialog.h"

#include "AEButton.h"
#include "AEValueField.h"
#include "Theme.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>

/* =============================================================================
 *  版式常量
 * =============================================================================
 *  面板高度固定 352，宽度随档位数变：
 *      宽 = N×120 + (N-1)×12 + 44
 *  其中 44 = 外框 3×2 + 内边距 19×2。
 *
 *  纵向（面板坐标）：
 *      3       外框
 *      3..63   标题栏 60        ← 关闭按钮 52×52 在里面，上下各留 4
 *      63..95  范围行 32        ← "min ~ max"，居中、次级字号（2026-10-31 新增）
 *      95..109 间距 14
 *      109..173 上排（加）64
 *      173..187 间距 14
 *      187..251 中排（数值 + 设置）64
 *      251..265 间距 14
 *      265..329 下排（减）64
 *      329..349 底边距 20
 *      349..352 外框
 * ========================================================================== */
namespace {

constexpr int kPanelH   = 352;          /* 320 + 32（范围行） */
constexpr int kFrame    = 3;

constexpr int kTitleH   = 60;
constexpr int kRangeH   = 32;           /* 范围行独占一条，别塞进标题栏 */
constexpr int kRowH     = 64;
constexpr int kRowGap   = 14;

constexpr int kColW     = 120;
constexpr int kColGap   = 12;
constexpr int kPadX     = 19;          /* 内容距外框内侧 */
constexpr int kPadTop   = 14;          /* 范围行与第一排之间 */

constexpr int kCloseSize = 52;         /* 右上关闭按钮 */

/* =============================================================================
 *  关闭符号：回车箭头「↵」（照着用户给的参考图重画）
 * =============================================================================
 *  【为什么不用 drawLine / drawPolygon】
 *    这块屏是像素风，画线会带抗锯齿边，和周围 1px 硬边不是一路；
 *    用"一格一格填方块"拼，边缘永远是硬的，放大 6 倍也没有灰边。
 *
 *  【历史】第一版硬抄参考图的"回"形箭头，7 个矩形堆在 16×20 网格里，
 *    缩进 52px 按钮后糊成一团白疙瘩；第二版换成 22px 的像素「×」，
 *    用户说"还是图片这种好看" —— 这版按参考图重新排了网格：
 *    10 列 × 8 行、每格 3px（= 30×24），笔画粗细 3px，和参考图的比例一致。
 *
 *  【形状】左侧一个指向左的箭头（chevron 三格递进），箭杆向右，
 *    在右端折下、再沿底边折回左边 —— 就是键盘上的回车符号。
 * ========================================================================== */
constexpr int kCell    = 3;    /* 每个像素格的边长 */
constexpr int kArtCols = 12;
constexpr int kArtRows = 10;

/*  '#' = 落墨。按参考图逐格还原：
 *    箭杆占 2 行（6px），右端竖线占 2 列，底边折回也占 2 行，
 *    箭头两齿各 1 格（两级台阶）—— 线宽 6/36 = 1/6，和参考图一致。
 *  【为什么是 10 行而不是 11】52px 方块去掉四层结构只剩 36px 填充，
 *    11 行 × 3 = 33 会顶到亮线，减掉中间一行"纯竖线"后 30px 刚好。
 */
const char *const kReturnArt[kArtRows] = {
    "..#.........",
    ".##.........",
    "############",
    "############",
    ".##.......##",
    "..#.......##",
    "..........##",
    "..........##",
    "############",
    "############",
};

void paintReturnIcon(QPainter &p, const QRect &area, const QColor &c, int dy)
{
    const int w = kArtCols * kCell;
    const int h = kArtRows * kCell;
    const int ox = area.left() + (area.width()  - w) / 2;
    const int oy = area.top()  + (area.height() - h) / 2 + dy;

    for (int r = 0; r < kArtRows; ++r) {
        const char *row = kReturnArt[r];
        for (int col = 0; col < kArtCols; ++col) {
            if (row[col] == '#')
                p.fillRect(QRect(ox + col * kCell, oy + r * kCell, kCell, kCell), c);
        }
    }
}

/* ---- 步进档位：按上限分档，最高 1000 ----
 * <=10   → {1, 10}
 * <=100  → {1, 10, 100}
 * >100   → {1, 10, 100, 1000}   上限再大也停在 4 档（用户定：最高 1000）
 */
QList<int> stepsForMax(int maxValue)
{
    QList<int> s;
    s << 1 << 10;
    if (maxValue > 10)
        s << 100;
    if (maxValue > 100)
        s << 1000;
    return s;
}

} // namespace

/* =============================================================================
 *  关闭按钮：深色方块 + 白色回车箭头（按用户给的参考图）
 * =============================================================================
 *  【为什么不复用 AEButton 的按钮本体】
 *    参考图那种"深色底 + 白色像素图标"和弹窗里其它浅色按钮不是一个东西，
 *    硬套浅色底再叠白符号（上一版）对比度不够、也不像参考图。
 *    这里整个按钮自己画：深色外框 → 深灰蓝面 → 1px 内圈 → 白色箭头。
 *
 *  【按下反馈】
 *    面变暗一档（#4D4D67）、内圈更深、箭头略灰 —— 像素风里靠"整体压暗"表示按下，
 *    比变蓝更贴近参考图的配色。
 * ========================================================================== */
namespace {

class CloseButton : public AEButton
{
public:
    explicit CloseButton(QWidget *parent) : AEButton(QString(), parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        /* ---- 四层结构：和 AEButton 同一套语言（上亮下暗 = 凸起）----
         *   外框 → U 形亮边 → 面 → 底部亮线 → 底部阴影
         *   按下：阴影 6 → 4（填充多 2px）、面压暗、图标下移 1px
         *   【自洽校验】52 = Frame2 + Edge2 + fill36 + Line2 + Shadow6 + Frame2
         *   改任何一个厚度，这个算式都要重新对一遍。 */
        const bool down = isDown();

        const QColor frame = QColor(Theme::AE::CloseShadow);      /* 外框与阴影同色 */
        const QColor edge  = QColor(down ? Theme::AE::CloseEdgeDown : Theme::AE::CloseEdge);
        const QColor face  = QColor(down ? Theme::AE::CloseFaceDown : Theme::AE::CloseFace);
        const QColor line  = QColor(Theme::AE::CloseLine);
        const QColor ink   = QColor(down ? Theme::AE::CloseInkDown  : Theme::AE::CloseInk);

        const QRect r = rect();
        p.fillRect(r, frame);

        const QRect in = r.adjusted(Theme::Metric::ChipFrame, Theme::Metric::ChipFrame,
                                    -Theme::Metric::ChipFrame, -Theme::Metric::ChipFrame);

        const int shadowH = down ? Theme::Metric::ChipShadowDown : Theme::Metric::ChipShadow;
        const int fillW = in.width()  - 2 * Theme::Metric::ChipEdge;
        const int fillH = in.height() - Theme::Metric::ChipEdge
                                      - Theme::Metric::ChipLine - shadowH;
        if (fillW <= 0 || fillH <= 0)
            return;

        /* U 形亮边：先铺满内层，面和阴影会把中间、底部盖掉，剩下的自然是「上 + 左右」 */
        p.fillRect(in, edge);

        const QRect faceRect(in.left() + Theme::Metric::ChipEdge,
                             in.top()  + Theme::Metric::ChipEdge,
                             fillW, fillH);
        p.fillRect(faceRect, face);

        p.fillRect(QRect(in.left(), faceRect.bottom() + 1,
                         in.width(), Theme::Metric::ChipLine), line);
        p.fillRect(QRect(in.left(), faceRect.bottom() + 1 + Theme::Metric::ChipLine,
                         in.width(), shadowH), frame);

        paintReturnIcon(p, faceRect, ink, down ? 1 : 0);
    }
};

} // namespace

AENumberDialog::AENumberDialog(QWidget *owner) : QWidget(owner)
{
    /* 默认隐藏；open() 时才 show */
    hide();
    setAttribute(Qt::WA_NoSystemBackground);
}

void AENumberDialog::open(const QString &title, int min, int max, int value)
{
    m_min   = min;
    m_max   = max;
    m_value = qBound(min, value, max);

    rebuild(title);

    /* 铺满 owner，放在最上层 */
    if (QWidget *o = parentWidget())
        setGeometry(0, 0, o->width(), o->height());
    show();
    raise();
}

void AENumberDialog::rebuild(const QString &title)
{
    /* ---- 清掉上一次的控件（档位数可能变了）---- */
    for (AEButton *b : m_buttons) {
        b->hide();
        b->deleteLater();
    }
    m_buttons.clear();

    if (m_ok)       { m_ok->hide();       m_ok->deleteLater();       m_ok = nullptr; }
    if (m_valueBox) { m_valueBox->hide(); m_valueBox->deleteLater(); m_valueBox = nullptr; }
    if (m_title)    { m_title->hide();    m_title->deleteLater();    m_title = nullptr; }

    /* ---- 档位与面板尺寸 ---- */
    m_steps = stepsForMax(m_max);
    const int N = m_steps.size();

    const int panelW = N * kColW + (N - 1) * kColGap + 2 * (kFrame + kPadX);

    const int ownerW = parentWidget() ? parentWidget()->width()  : 1024;
    const int ownerH = parentWidget() ? parentWidget()->height() : 600;
    m_panelRect = QRect((ownerW - panelW) / 2, (ownerH - kPanelH) / 2, panelW, kPanelH);

    const QPoint org = m_panelRect.topLeft();
    const int contentX = org.x() + kFrame + kPadX;   /* 内容区左边界 */

    auto colX = [&](int i) { return contentX + i * (kColW + kColGap); };

    /* ---- 范围行：单独一条，居中画（见文件头的版式图）---- */
    m_rangeText = QString::fromUtf8("范围 %1 ~ %2").arg(m_min).arg(m_max);
    m_rangeRect = QRect(org.x() + kFrame,
                        org.y() + kFrame + kTitleH,
                        panelW - 2 * kFrame,
                        kRangeH);

    /* ---- 标题 ---- */
    m_title = new QLabel(title, this);
    m_title->setGeometry(org.x(), org.y() + kFrame, panelW, kTitleH);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setStyleSheet(Theme::qssTextFlat());
    m_title->setFont(font());
    m_title->show();

    /* ---- 右上：关闭按钮（× 符号，见 CloseButton 的说明）---- */
    CloseButton *close = new CloseButton(this);
    close->setGeometry(org.x() + panelW - kFrame - 4 - kCloseSize,
                       org.y() + kFrame + (kTitleH - kCloseSize) / 2,
                       kCloseSize, kCloseSize);
    connect(close, &QPushButton::clicked, this, &AENumberDialog::hide);
    close->show();
    m_buttons.append(close);

    /* ---- 上排：加档 ---- */
    const int rowAddY = org.y() + kFrame + kTitleH + kRangeH + kPadTop;
    for (int i = 0; i < N; ++i) {
        AEButton *b = new AEButton(QString("+%1").arg(m_steps.at(i)), this);
        b->setGeometry(colX(i), rowAddY, kColW, kRowH);
        const int step = m_steps.at(i);
        connect(b, &QPushButton::clicked, this, [this, step]() { applyStep(step); });
        b->show();
        m_buttons.append(b);
    }

    /* ---- 中排：数值显示 + 设置 ---- */
    const int rowMidY = rowAddY + kRowH + kRowGap;

    /* 数值显示占前 N-1 列（N=2 时正好一列），设置占第 N 列 ——
     * 和上下两排的按钮对齐同一条竖线 */
    const int valueW = (N - 1) * kColW + (N - 2) * kColGap;

    m_valueBox = new AEValueField(this);
    m_valueBox->setGeometry(contentX, rowMidY, valueW, kRowH);
    {
        /* 大字号：和正文 27px 拉开层次。只改像素尺寸，不改族名
         * （全局字体由 main.cpp 的 QApplication::setFont 统一决定）。 */
        QFont f = font();
        f.setPixelSize(36);
        m_valueBox->setFont(f);
    }
    m_valueBox->show();

    m_ok = new AEButton(QString::fromUtf8("设置"), this);
    m_ok->setGeometry(colX(N - 1), rowMidY, kColW, kRowH);
    connect(m_ok, &QPushButton::clicked, this, [this]() {
        const int v = m_value;
        hide();
        emit accepted(v);
    });
    m_ok->show();

    /* ---- 下排：减档 ---- */
    const int rowSubY = rowMidY + kRowH + kRowGap;
    for (int i = 0; i < N; ++i) {
        AEButton *b = new AEButton(QString("-%1").arg(m_steps.at(i)), this);
        b->setGeometry(colX(i), rowSubY, kColW, kRowH);
        const int step = m_steps.at(i);
        connect(b, &QPushButton::clicked, this, [this, step]() { applyStep(-step); });
        b->show();
        m_buttons.append(b);
    }

    refreshValue();
}

void AENumberDialog::applyStep(int delta)
{
    /* 只改副本，不下发 —— 点「设置」才提交，所以取消是真的取消 */
    m_value = qBound(m_min, m_value + delta, m_max);
    refreshValue();
}

void AENumberDialog::refreshValue()
{
    if (m_valueBox)
        m_valueBox->setValue(m_value);
}

void AENumberDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    /* ---- ① 遮罩：吃掉下层所有点击 ---- */
    p.fillRect(rect(), QColor(0, 0, 0, 160));

    /* ---- ② 面板：外框 + 内亮边 + 底色（和 AEValueField 同一个画法） ---- */
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(m_panelRect, QColor(Theme::AE::BtnFrame));

    const QRect in = m_panelRect.adjusted(kFrame, kFrame, -kFrame, -kFrame);
    p.fillRect(in, QColor(Theme::AE::BgLight));

    /* ---- ③ 范围行：居中、次级字号、次级色 ----
     *  ★ 只画在 m_rangeRect 里。上一版把矩形高度算成"标题栏 + 半块面板"、
     *    还靠 VCenter 定位，结果文字正好落在第一排按钮底下，被按钮盖掉一半。 */
    QFont rf = font();
    rf.setPixelSize(Theme::Font::Secondary);
    p.setFont(rf);
    p.setPen(QColor(Theme::AE::TextMuted));
    p.drawText(m_rangeRect, Qt::AlignCenter, m_rangeText);
}

void AENumberDialog::mousePressEvent(QMouseEvent *e)
{
    /* 点面板**内部**的空白处：什么都不做（别把"点到面板"当成取消）
     * 点面板外面（遮罩）：取消。
     *
     * 注意：按钮是子控件，点在按钮上不会走到这里。 */
    if (!m_panelRect.contains(e->pos()))
        hide();
}
