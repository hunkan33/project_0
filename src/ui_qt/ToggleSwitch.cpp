// SPDX-License-Identifier: MIT
/** @file ToggleSwitch.cpp
 *  @brief ToggleSwitch 绘制与交互
 */
#include "ToggleSwitch.h"
#include "Theme.h"

#include <QPainter>

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    /* QAbstractButton 默认不 checkable，必须显式打开，
     * 否则 isChecked() 恒为 false，点了也切不过去。 */
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);        // 和 AEButton 一致：不要焦点框
    setFixedSize(Theme::Toggle::Width, Theme::Toggle::Height);
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(Theme::Toggle::Width, Theme::Toggle::Height);
}

/* =============================================================================
 *  凸起灰格
 * =============================================================================
 *  纵向分层（以格高 30 为例）：
 *      y 0 .. 2    亮边 #ADB0C4   （3）
 *      y 3 .. 20   填充 #9A9FB4   （18）
 *      y21 .. 23   亮边 #ADB0C4   （3）
 *      y24 .. 29   底部阴影 #696D88（6）
 *  横向只有左右各 3px 亮边，中间 24px 是填充 —— 和 AEButton 的 U 形灰边同理：
 *  先整格铺亮边色，再用填充盖住中间，剩下的自然是亮边。
 * ========================================================================== */
void ToggleSwitch::drawRaised(QPainter &p, const QRect &cell, bool pressed) const
{
    const char *cEdge   = Theme::AE::BtnEdgeTop;
    const char *cBody   = Theme::AE::BtnFill;
    const char *cShadow = Theme::AE::ToggleTrack;

    if (!isEnabled()) {
        /* 禁用格子：纯色一块，没有内圈、没有凸起。
         * 两格之间靠外框（#413F54）的分隔带区分，不靠格子自己的描边。 */
        p.fillRect(cell, QColor(Theme::AE::ToggleTrack));   /* #696D88 */
        return;
    } else if (pressed) {
        /* 按下变蓝 —— 与 AEButton 的 active 态同一套配色 */
        cEdge   = Theme::AE::AccentLight;
        cBody   = Theme::AE::Accent;
        cShadow = Theme::AE::AccentDark;
    }

    const int b  = Theme::Toggle::Bevel;
    const int sh = Theme::Toggle::Shadow;

    /* ① 整格铺亮边色 */
    p.fillRect(cell, QColor(cEdge));

    /* ② 填充：左右各留 b，底部留 b（下亮边）+ sh（阴影） */
    const QRect body(cell.left() + b,
                     cell.top()  + b,
                     cell.width()  - 2 * b,
                     cell.height() - 2 * b - sh);
    if (body.width() > 0 && body.height() > 0)
        p.fillRect(body, QColor(cBody));

    /* ③ 底部阴影：通宽 */
    p.fillRect(QRect(cell.left(), cell.bottom() - sh + 1, cell.width(), sh),
               QColor(cShadow));
}

/* =============================================================================
 *  凹槽 —— 纯色，没有凸凹，视觉上是"凹进去的轨道"
 * ========================================================================== */
void ToggleSwitch::drawWell(QPainter &p, const QRect &well, bool on) const
{
    const char *c;
    if (!isEnabled())
        c = Theme::AE::ToggleTrack;        /* 凹槽在禁用态也走灰 */
    else
        c = on ? Theme::AE::Accent : Theme::AE::ToggleTrack;

    p.fillRect(well, QColor(c));
}

/* =============================================================================
 *  滑块
 * =============================================================================
 *  ON  ：凹槽里一根浅色竖条（3 × 15）
 *  OFF ：凹槽里一个 15×15 的方环，四角各留 3×3 缺口 —— 参考图里就是这么画的，
 *        不是实心圆点，也不是完整方框。
 * ========================================================================== */
void ToggleSwitch::drawKnob(QPainter &p, const QRect &well, bool on, bool pressed) const
{
    const int bw = Theme::Toggle::BarW;
    const int bh = Theme::Toggle::BarH;
    const int k  = Theme::Toggle::Knob;
    const int g  = Theme::Toggle::KnobGap;

    if (on) {
        /* +1 的偏移是为了让 3px 宽的竖条在偶数宽格子里居中时不吃亏 */
        const QRect bar(well.left() + (well.width()  - bw + 1) / 2,
                        well.top()  + (well.height() - bh + 1) / 2,
                        bw, bh);
        p.fillRect(bar, QColor(pressed ? Theme::AE::Accent
                                       : Theme::AE::AccentLight));
        return;
    }

    const QRect ring(well.left() + (well.width()  - k + 1) / 2,
                     well.top()  + (well.height() - k + 1) / 2,
                     k, k);
    const char *c = isEnabled() ? Theme::AE::ToggleKnob : Theme::AE::TextMuted;

    /* 四条边，中间留空、四角留缺口 */
    p.fillRect(QRect(ring.left() + g,     ring.top(),          k - 2 * g, g), QColor(c)); /* 上 */
    p.fillRect(QRect(ring.left() + g,     ring.bottom() - g + 1, k - 2 * g, g), QColor(c)); /* 下 */
    p.fillRect(QRect(ring.left(),         ring.top() + g,      g, k - 2 * g), QColor(c)); /* 左 */
    p.fillRect(QRect(ring.right() - g + 1, ring.top() + g,     g, k - 2 * g), QColor(c)); /* 右 */
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    /* 三个 render hint 和 AEButton.cpp 一致：
     * 不关抗锯齿，像素风立刻退化成糊边。 */
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QRect r = rect();
    if (r.width() < Theme::Toggle::Width || r.height() < Theme::Toggle::Height)
        return;

    const bool on      = isChecked();
    const bool pressed = isDown();

    /* ---- ① 外框：整块铺深色，四周留 Frame 宽的一圈 ---- */
    /* 外框不分禁用与否，恒 #413F54 —— AE2 的 button_disabled 也保留深色外框 */
    p.fillRect(r, QColor(Theme::AE::BtnFrame));

    /* ---- ② 两个槽位：凹槽跟着滑块走 ---- */
    const int f = Theme::Metric::Frame;
    const int c = Theme::Toggle::Cell;
    const int d = Theme::Toggle::Divider;

    const QRect slotL(f, f, c, c);
    const QRect slotR(f + c + d, f, c, c);

    const QRect &well   = on ? slotL : slotR;
    const QRect &raised = on ? slotR : slotL;

    drawWell(p, well, on);
    drawRaised(p, raised, pressed);

    /* ---- ③ 滑块画在凹槽里 ---- */
    drawKnob(p, well, on, pressed);
}
