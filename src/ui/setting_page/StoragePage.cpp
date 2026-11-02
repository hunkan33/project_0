// SPDX-License-Identifier: MIT
/** @file StoragePage.cpp
 *  @brief 存储页实现
 */
#include "StoragePage.h"

#include "AEButton.h"
#include "Theme.h"
#include "storage.h"        /* storage_mounted / storage_capacity */
#include "page_chrome.h"    /* 底部按钮行几何（三页共用） */

#include <QApplication>

namespace {

/* ---- 版式 ----
 * 标题占 40..96（AEPage 画的），内容从 130 起。
 * 底部按钮 y = 524 是全工程统一的行（见 AGENT.md §6.9），和设置页/相机页对齐。 */
constexpr int kLabelX = 60;
constexpr int kLabelW = 240;
constexpr int kValueX = 320;
constexpr int kValueW = 640;
constexpr int kRowH   = 60;

constexpr int kRowState = 130;
constexpr int kRowTotal = 190;
constexpr int kRowAvail = 250;
constexpr int kRowUsed  = 310;
constexpr int kRowMount = 380;
constexpr int kRowDir   = 440;

/* KB → "62.9 GB" / "512 MB"。
 * 卡是 63G 量级，用 GB 一位小数；小于 1GB 时退到 MB（剩余快满时更有信息量）。 */
QString fmtKb(unsigned long long kb)
{
    const double mb = (double)kb / 1024.0;
    if (mb >= 1024.0)
        return QString("%1 GB").arg(mb / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg((qulonglong)mb);
}

} // namespace

StoragePage::StoragePage(QWidget *parent) : AEPage(parent)
{
    setPageTitle(QString::fromUtf8("存储"));
    setFont(QApplication::font());

    m_valState = addRow(kRowState, QString::fromUtf8("存储卡"));
    m_valTotal = addRow(kRowTotal, QString::fromUtf8("总容量"));
    m_valAvail = addRow(kRowAvail, QString::fromUtf8("可用容量"));
    m_valUsed  = addRow(kRowUsed,  QString::fromUtf8("已用容量"));
    m_valMount = addRow(kRowMount, QString::fromUtf8("挂载点"));
    m_valDir   = addRow(kRowDir,   QString::fromUtf8("录像目录"));

    /* 挂载点和录像目录是固定文字，不进 refresh() —— 它们是配置不是状态 */
    m_valMount->setText(QString::fromLatin1(STORAGE_ROOT));

    /* 「返回」—— 只上报事件，不自己改状态（和设置页/相机页一致） */
    m_btnBack = PageChrome::makeBackButton(this);
    connect(m_btnBack, &QPushButton::clicked, this, &StoragePage::backRequested);
}

QLabel *StoragePage::addRow(int y, const QString &label)
{
    QLabel *l = new QLabel(label, this);
    l->setGeometry(kLabelX, y, kLabelW, kRowH);
    l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    /* 标签用灰字：它只说"这一行是什么"，不是重点 */
    l->setStyleSheet(Theme::qssTextFlat(Theme::AE::TextMuted));

    QLabel *v = new QLabel(this);
    v->setGeometry(kValueX, y, kValueW, kRowH);
    v->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    v->setStyleSheet(Theme::qssTextFlat());
    return v;
}

void StoragePage::onEnter()
{
    refresh();
    show();
    raise();
    /* 同步刷：面板要盖住 DE 视频层，不能等下一帧 */
    repaint();
}

void StoragePage::onLeave()
{
    hide();
}

void StoragePage::refresh()
{
    /* 判"卡在不在"必须用 storage_mounted()（查 /proc/mounts），
     * 不能用 access() / statvfs —— 卡拔了挂载点目录还在，会被当成"有卡"。
     * 详见 storage.h 的说明。 */
    unsigned long long totalKb = 0, availKb = 0;

    if (storage_capacity(STORAGE_ROOT, &totalKb, &availKb) != 0) {
        m_valState->setText(QString::fromUtf8("未插入"));
        m_valTotal->setText(QString::fromUtf8("—"));
        m_valAvail->setText(QString::fromUtf8("—"));
        m_valUsed->setText(QString::fromUtf8("—"));
        m_valDir->setText(QString::fromUtf8("—"));
        return;
    }

    m_valState->setText(QString::fromUtf8("已安装"));
    m_valTotal->setText(fmtKb(totalKb));
    m_valAvail->setText(fmtKb(availKb));
    m_valUsed->setText(fmtKb(totalKb > availKb ? totalKb - availKb : 0));

    /* 录像目录：录制落盘还没做（见 16 号文档），这里如实写"未实现"，
     * 不要写一个看起来能用、点进去却是空的路径。 */
    m_valDir->setText(QString::fromUtf8("%1/DCIM　（录制落盘未实现）")
                          .arg(QString::fromLatin1(STORAGE_ROOT)));
}
