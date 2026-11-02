// SPDX-License-Identifier: MIT
/** @file page_chrome.cpp
 *  @brief page_chrome.h 的实现
 */
#include "page_chrome.h"

#include "AEButton.h"
#include "page.h"        /* SCR_W */

#include <QTimer>
#include <QWidget>

namespace PageChrome {

AEButton *makeBackButton(QWidget *parent)
{
    AEButton *b = new AEButton(QString::fromUtf8("返回"), parent);
    b->setGeometry(kBtnMargin, kBtnY, kBtnW, kBtnH);
    return b;
}

AEButton *makeSaveButton(QWidget *parent)
{
    AEButton *b = new AEButton(QString::fromUtf8("保存"), parent);
    b->setGeometry(SCR_W - kBtnMargin - kBtnW, kBtnY, kBtnW, kBtnH);
    return b;
}

AEButton *makeCenterButton(const QString &text, QWidget *parent)
{
    AEButton *b = new AEButton(text, parent);
    b->setGeometry((SCR_W - kBtnW) / 2, kBtnY, kBtnW, kBtnH);
    return b;
}

void flashButtonText(AEButton *btn, const QString &text, int ms, const QString &restoreText)
{
    if (!btn)
        return;

    btn->setText(text);
    /* 用 btn 作为 singleShot 的上下文对象：按钮比页面活得久更安全，
     * 也避免 lambda 捕获 this 造成的悬垂。 */
    QTimer::singleShot(ms, btn, [btn, restoreText]() { btn->setText(restoreText); });
}

} // namespace PageChrome
