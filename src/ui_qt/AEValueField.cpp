// SPDX-License-Identifier: MIT
/** @file AEValueField.cpp
 *  @brief AEValueField 绘制实现
 */
#include "AEValueField.h"

#include "Theme.h"

#include <QPainter>

/* 厚度沿用 Theme::Metric —— 和 AEButton / ToggleSwitch 同一套，改外观只改 Theme.h */
namespace {
using namespace Theme::Metric;
}

AEValueField::AEValueField(QWidget *parent) : QWidget(parent)
{
}

void AEValueField::setValue(int v)
{
    m_text = QString::number(v);
    update();
}

int AEValueField::value() const
{
    return m_text.toInt();
}

void AEValueField::setPlaceholder(const QString &text)
{
    m_text = text;
    update();
}

void AEValueField::setUsable(bool on)
{
    if (m_usable == on)
        return;
    m_usable = on;
    update();
}

void AEValueField::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    /* 像素风：和 AEButton 一样关抗锯齿，否则外框的直角会发虚 */
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QRect r = rect();
    if (r.width() <= 2 * Frame || r.height() <= 2 * Frame)
        return;

    /* 颜色：可用时用按钮的填充色（和旁边的「修改」按钮同一档亮度，
     * 但少了底部阴影，所以一眼能看出"这是字段不是按钮"）；
     * 不可用时整体压到禁用色，和禁用按钮同一套。 */
    const char *cEdge = m_usable ? Theme::AE::BtnEdgeTop : Theme::AE::ToggleKnob;
    const char *cFill = m_usable ? Theme::AE::BtnFill    : Theme::AE::ToggleTrack;
    const char *cText = m_usable ? Theme::AE::BtnText    : Theme::AE::DisabledText;

    /* ① 外框（四周） */
    p.fillRect(r, QColor(Theme::AE::BtnFrame));

    /* ② 内层：先把整个内层铺亮边色，再用填充盖中间 ——
     *    剩下的自然就是四周一圈等宽的亮边（上下左右对称，不做 U 形） */
    const QRect in = r.adjusted(Frame, Frame, -Frame, -Frame);
    p.fillRect(in, QColor(cEdge));

    const int inset = Edge;
    const QRect body = in.adjusted(inset, inset, -inset, -inset);
    if (body.width() <= 0 || body.height() <= 0)
        return;
    p.fillRect(body, QColor(cFill));

    /* ③ 文字：居中 */
    p.setPen(QColor(cText));
    p.setFont(font());
    p.drawText(body, Qt::AlignCenter, m_text);
}
