// SPDX-License-Identifier: MIT
/** @file AEPage.cpp
 *  @brief AEPage 绘制实现
 */
#include "AEPage.h"
#include <QApplication>
#include <QPainter>

namespace {
/* 标题栏位置与高度（沿用顶栏的 48 行视觉节奏）
 *
 * 【字号不在这里定义】
 *   页面标题和正文用同一个字号（跟随应用默认字体），颜色同 Theme::AE::Text。
 *   标题靠"居中 + 上方 40px 留白"和正文拉开层次，不再放大字号 ——
 *   全屏页面上只有一种字，视觉才是一整块。
 *   这里只留"标题栏摆在哪、多高"这类几何量。 */
constexpr int kTitleTop    = 40;
constexpr int kTitleHeight = 56;
}

AEPage::AEPage(QWidget *parent) : QWidget(parent)
{
    /* 铺满整屏。少一个像素，那个像素就是视频。 */
    setGeometry(0, 0, SCR_W, SCR_H);

    /* 注意：这里绝不要设 Qt::WA_OpaquePaintEvent。
     * 它的语义是"我的 paintEvent 会画满，你别先擦背景" ——
     * 设了却漏画，没画到的地方就会露出下面的 DE 视频层。 */

    /* 这个字体是 AEPage 及其所有子控件的【默认字体】。
     * 注意 AEButton 是自绘的（paintEvent 里 p.setFont(font())），
     * 它不读任何 QSS（Theme.h 里现在也已经没有按钮样式了）——
     * 按钮字号实际就来自这里。要改按钮字号，改 main.cpp 里那个 QFont::setPixelSize(27)。
     * （`Theme::Font::Body` 已删除，别再照旧注释去找。） */
    /* 【页面字体 = 应用默认字体（Source Han Sans CN 27px，抗锯齿）】
     * 2026-10-29 用户定方向：设置页不再用像素字体 Zpix。
     * 底栏四个按钮实际渲染的也是这套字体（AEButton 只继承 QApplication::font()），
     * 所以这里显式跟随它，页面和底栏才是同一套字。
     *
     * 不要改回像素字体时代那套（已删除的 Theme::Font::Body，24px 网格），
     * 只有配 Zpix + NoAntialias 才成立，用在思源上会偏小且不和底栏对齐。 */
    setFont(QApplication::font());

    m_title = new QLabel(this);
    m_title->setGeometry(0, kTitleTop, SCR_W, kTitleHeight);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setStyleSheet(Theme::qssTextFlat());
    m_title->hide();                 /* 子类调 setPageTitle() 之后才显示 */
}

void AEPage::setPageTitle(const QString &text)
{
    m_title->setText(text);

    /* 标题不放大字号：全页面只有 Body 一种字。
     * font() 就是上面 setFont() 进来的那套（应用默认字体），
     * QLabel 本来也会继承，这里显式设一遍，省得以后有人给标题另设字体。 */
    m_title->setFont(font());

    m_title->show();
}

void AEPage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r  = rect();
    const int   bw = Theme::Metric::BevelWidth;      /* 2px */

    /* ① 刷满整个矩形 —— 这一步保证视频被彻底遮住。
     *    不依赖样式表、不依赖 autoFillBackground、不依赖 Qt 的擦除策略。 */
    p.fillRect(r, QColor(Theme::AE::BgPanel));

    /* ② 凸凹边框：左上亮、右下暗（和 Theme::qssPanel() 的观感一致）。
     *    坐标内缩半个线宽，避免线被裁掉一半。 */
    const int h = bw / 2;

    p.setPen(QPen(QColor(Theme::AE::BevelLight), bw));
    p.drawLine(r.topLeft()    + QPoint(h, h),  r.topRight()    + QPoint(-h, h));
    p.drawLine(r.topLeft()    + QPoint(h, h),  r.bottomLeft()  + QPoint(h, -h));

    p.setPen(QPen(QColor(Theme::AE::BevelDark), bw));
    p.drawLine(r.bottomLeft() + QPoint(h, -h), r.bottomRight() + QPoint(-h, -h));
    p.drawLine(r.topRight()   + QPoint(-h, h), r.bottomRight() + QPoint(-h, -h));
}
