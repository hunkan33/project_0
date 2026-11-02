// SPDX-License-Identifier: MIT
/** @file AEPage.h
 *  @brief 全屏页面基类（凸凹面板 + 标题）
 */
#ifndef AEPAGE_H
#define AEPAGE_H

#include <QWidget>
#include <QLabel>
#include "Theme.h"
#include "page.h"          /* SCR_W / SCR_H */

/* =============================================================================
 *  AEPage —— 全屏遮挡页基类（AE 像素风全家桶的"页面"成员，与 AEButton 配套）
 * =============================================================================
 *  【它解决什么问题】
 *    这块屏幕上叠着两层：ch[0] 视频层（DE 硬件）+ ch[1] fb0（Qt 画）。
 *    fb0 某块 alpha=0 → 视频从那块透出；alpha=255 → 视频被遮住。
 *
 *    所以任何"要盖住视频的全屏页面"，必须保证自己那 1024×600 的
 *    每一个像素都是不透明的。少一个像素，那个像素就是视频。
 *
 *  【为什么不能用 QSS 实现】
 *    QSS 的 background 会受 border、padding、样式表解析顺序、
 *    Qt 的背景擦除策略影响，任何一环没走到就会漏。
 *    所以这里在 paintEvent 里用 fillRect 直接刷满，不依赖任何样式表机制。
 *    代价是边框要自己画（四行 drawLine），换来的是 100% 可预测。
 *
 *  【新页面怎么用】
 *      class PlaybackPage : public AEPage {
 *      public:
 *          explicit PlaybackPage(QWidget *parent = nullptr)
 *              : AEPage(parent) {
 *              setPageTitle("回放");           // 可选，不调就没有标题
 *              // 加自己的控件……
 *          }
 *      };
 *
 *    · 铺满整屏、背景不透明、像素风边框  —— 全部自动继承
 *    · 子类不要再设 setGeometry / setStyleSheet / paintEvent
 *    · 子类若要自定义绘制，必须先调 AEPage::paintEvent(e)
 * ========================================================================== */
class AEPage : public QWidget
{
    Q_OBJECT

public:
    /** @brief 构造一个全屏页面。 @param parent 父控件（通常传 nullptr）。 */
    explicit AEPage(QWidget *parent = nullptr);

protected:
    /** @brief 设置居中标题。
     *  @param text 标题文字。
     *  @note 子类在构造函数里调一次；不调则不显示标题。
     */
    void setPageTitle(const QString &text);

    /** @brief 画凸凹面板背景。
     *  @note 子类如需额外绘制，**先调基类版本**刷满背景，再画自己的东西。
     */
    void paintEvent(QPaintEvent *e) override;

private:
    QLabel *m_title = nullptr;   ///< 居中标题；nullptr = 本页无标题
};

#endif /* AEPAGE_H */
