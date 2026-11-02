// SPDX-License-Identifier: MIT
/** @file AEButton.cpp
 *  @brief AEButton 的五层绘制
 */
#include "AEButton.h"
#include "Theme.h"

#include <QPainter>

/*
 * 各层厚度（像素）
 * -----------------------------------------------------------------------------
 *  按截图 237px 高按钮等比换算到我们的 56px（比例 0.236）：
 *      截图             我们
 *   ─────────────────────────────────
 *      外框    12px  →   3px
 *      灰边    12px  →   3px
 *      填充   155px  →  36px
 *      亮线    12px  →   3px
 *      阴影    35px  →   8px
 *      外框    12px  →   3px
 *                      ─────
 *                        56px
 */
namespace {
/* 厚度统一由 Theme::Metric 提供 —— ToggleSwitch 用的是同一套，
 * 改外观只改 Theme.h 一处，两个控件不会各自跑偏。
 *
 * 这里只是取别名，数值一个字都没改：
 *   ② 底部阴影常态 8px、按下 6px（实测截图：常态占高 14.8%，按下占 10.5%，
 *      相对少 29%，视觉上就是"按下去以后底部变薄了"） */
using Theme::Metric::Frame;
using Theme::Metric::Edge;
using Theme::Metric::Line;
using Theme::Metric::Shadow;
using Theme::Metric::ShadowDown;
}

AEButton::AEButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    setFocusPolicy(Qt::NoFocus);        // 焦点框去除
}

AEButton::AEButton(QWidget *parent)
    : QPushButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
}

void AEButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    //关抗锯齿
    p.setRenderHint(QPainter::Antialiasing, false);
    //最近邻插值
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    //超越检查
    const QRect r = rect();
    if (r.width() <= 2 * Frame || r.height() <= 2 * Frame)
        return;

    //按住 或 (toggle 模式且 checked) 时变蓝标志位
    const bool active = isDown() || (m_toggleMode && isChecked());

    const char *cFrame  = Theme::AE::BtnFrame;                                      // 外框：永远不变
    const char *cEdge   = active ? Theme::AE::AccentLight : Theme::AE::BtnEdgeTop;  // 上方灰边：按下时变蓝
    const char *cFill   = active ? Theme::AE::Accent      : Theme::AE::BtnFill;     // 填充：按下时变蓝
    const char *cShadow = active ? Theme::AE::AccentDark  : Theme::AE::BtnShadow;   // 底部阴影：按下时变薄，颜色也变深
    const char *cText   = active ? Theme::AE::AccentText  : Theme::AE::BtnText;     // 文字：按下时变深色
    /* 禁用态用 AE2 button_disabled 的灰 —— 和 AEComboBox / ToggleSwitch 同一套。
     * 2026-10-31 之前这里是 BgLight/BgLight/BgPanel（发白的空框），
     * 和另外三处禁用态不一致：同一个"不可点"在界面上会长出两种样子。 */
    if (!isEnabled()) {
        cEdge   = Theme::AE::ToggleKnob;     /* #878FA5 亮边 */
        cFill   = Theme::AE::ToggleTrack;    /* #696D88 主体 */
        cShadow = Theme::AE::TextMuted;      /* #5A5F75（禁用时不画） */
        cText   = Theme::AE::DisabledText;   /* #B2B6CA 低对比灰字 */
    }

    // 外框：不分启用/禁用都画（AE2 的 button_disabled 也保留深色外框）
    p.fillRect(r, QColor(cFrame));

    /* ---- 内层（去掉外框厚度）---- */
    const QRect in = r.adjusted(Frame, Frame, -Frame, -Frame);

    /* =========================================================================
     *  两种结构，不是两套颜色
     * =========================================================================
     *   启用：外框 → U 形灰边 → 填充 → 底部亮线 → 底部阴影（上亮下暗 = 凸起）
     *   禁用：外框 → 纯色一块（没有 U 形边、没有亮线、没有阴影 = 扁平）
     *
     *   禁用为什么必须扁平：AE2 的 button_disabled.png 实测是
     *   「外框1 + 亮边1 + 主体13 + 亮边1 + 底部1px(与主体同色)」，等于没有阴影。
     *   AEComboBox / ToggleSwitch 的禁用态已经按这个画了，这里是第三处 ——
     *   三处必须一致，否则同一个"不可点"会呈现三种外观。
     *
     *   【自洽校验】56px 高的按钮，启用且常态下：
     *       Frame(3) + Edge(3) + fillH + Line(3) + Shadow(8) + Frame(3) = 56
     *       → fillH = 56 - 3*2 - 3 - 3 - 8 = 36
     *   按下时 ShadowDown=6，fillH 变成 38 —— 阴影少 2px、填充多 2px，
     *   视觉上就是"按下去变扁"。这是按钮手感的来源，不是错觉。
     *   改 Theme::Metric 里任何一个厚度，这个算式都要重新对一遍。
     * ====================================================================== */
    const int shadowH = active ? ShadowDown : Shadow;
    const int fillW = in.width() - 2 * Edge;
    const int fillH = in.height() - Edge - Line - shadowH;
    if (fillW <= 0 || fillH <= 0)
        return;

    /* 文字排版区：两种状态共用，避免禁用时文字位置跳动 */
    const QRect fillRect(in.left() + Edge,
                         in.top()  + Edge,
                         fillW, fillH);

    if (isEnabled()) {
        /* U 形灰边：先铺满内层，后面用填充和底部亮线/阴影盖掉中间和底部，
         * 剩下的自然就是「上 + 左右」的 U 形 */
        p.fillRect(in, QColor(cEdge));
        p.fillRect(fillRect, QColor(cFill));

        /* 底部亮线（通宽，盖住灰边） */
        p.fillRect(QRect(in.left(), fillRect.bottom() + 1, in.width(), Line),
                   QColor(cEdge));

        /* 底部阴影（通宽，按下时变薄） */
        p.fillRect(QRect(in.left(), fillRect.bottom() + 1 + Line, in.width(), shadowH),
                   QColor(cShadow));
    } else {
        /* 禁用：外框内整块一个颜色，别的什么都不画 */
        p.fillRect(in, QColor(cFill));
    }

    /* ---- ⑤ 文字：居中 ---- */
    p.setPen(QColor(cText));
    p.setFont(font());
    p.drawText(fillRect, Qt::AlignCenter, text());
}
