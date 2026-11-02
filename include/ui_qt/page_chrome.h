// SPDX-License-Identifier: MIT
/** @file page_chrome.h
 *  @brief 全屏页的公共装饰：底部按钮行几何 + 按钮文字反馈
 *
 *  【为什么有这个文件】
 *    三个全屏页（SettingsPage / CameraPage / StoragePage）各自写了一遍
 *    `setGeometry(30, 524, 200, 56)`。改一次按钮尺寸要改三处，漏一处两页就错位 ——
 *    这不是"代码好不好看"，是**单一真相源**问题。
 *    `flashSaved()` 同理：两页各有一份 800ms 复位逻辑，必须靠人手工保持一致。
 *
 *  【为什么不做成公共基类 FormPage】
 *    基类要把 `backRequested()` 信号搬上去、把 `onSave()` 改成虚函数，
 *    会动到 3 个页面的类声明与 moc 归属；为省约 40 行付这个风险不划算。
 *    这里只收口"数据"和"纯函数"，**不动类结构**。
 */
#ifndef _PAGE_CHROME_H_
#define _PAGE_CHROME_H_

#include <QString>

class QWidget;
class AEButton;

namespace PageChrome {

/* ---- 底部按钮行几何：三页共用，**只在这里改** ----
 * 524：CameraPage 有个 7 行视口占 96..516，按钮必须在 516 之下留 8px；
 *      设置页本来用 512，跟着统一到 524，切页时按钮不会上下跳 12px。 */
constexpr int kBtnY      = 524;   ///< 按钮行顶部 y
constexpr int kBtnW      = 200;   ///< 按钮宽
constexpr int kBtnH      = 56;    ///< 按钮高
constexpr int kBtnMargin = 30;    ///< 距屏幕左右边缘的距离

/** @brief 左下「返回」按钮（已定位，**调用方自己 connect**）。
 *  @param parent 按钮挂在哪个控件下（通常是页面自己）。 */
AEButton *makeBackButton(QWidget *parent);

/** @brief 右下「保存」按钮（已定位，**调用方自己 connect**）。
 *  @param parent 按钮挂在哪个控件下（通常是页面自己）。 */
AEButton *makeSaveButton(QWidget *parent);

/** @brief 底部居中的按钮（设置页的「摄像头设置」）。
 *  @param text   按钮文字。
 *  @param parent 按钮挂在哪个控件下。 */
AEButton *makeCenterButton(const QString &text, QWidget *parent);

/** @brief 让按钮短暂显示一段文字，`ms` 毫秒后恢复。
 *
 *  @param restoreText 恢复后的文字，默认「保存」
 *  @note 两页共用同一套时长，观感才一致。
 */
void flashButtonText(AEButton *btn, const QString &text,
                     int ms = 800,
                     const QString &restoreText = QString::fromUtf8("保存"));

} // namespace PageChrome

#endif /* _PAGE_CHROME_H_ */
