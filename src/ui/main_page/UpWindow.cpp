// SPDX-License-Identifier: MIT
/** @file UpWindow.cpp
 *  @brief 顶栏实现
 */
#include "UpWindow.h"
#include <QApplication>

#include "storage.h"   /* 判卡在不在 + 读容量 */

UpWindow::UpWindow(QWidget *parent) : QWidget(parent)
{
    /* ---- 窗口本身：只占顶栏 48 行 ---- */
    setGeometry(0, 0, 1024, 48);
    setAttribute(Qt::WA_StyledBackground, true);   // ← 必须
    setStyleSheet(Theme::qssPanel());

    /* 顶栏文字 = 应用默认字体（Source Han Sans CN 27px，抗锯齿）。
     * 2026-10-29 用户定：全屏不再用像素字体 Zpix，顶栏跟着一起改。
     * 不要再写 QFont(Theme::Font::Family) —— Theme::Font 里那套是 Zpix 口径。 */
    setFont(QApplication::font());

    /* ---- 时间（左）---- */
    m_lblTime = new QLabel("00:00:00", this);
    m_lblTime->setGeometry(16, 6, 220, 36);
    m_lblTime->setStyleSheet(Theme::qssTextFlat());

    /* ---- 录制状态（中）---- */
    m_lblRec = new QLabel("就绪", this);
    m_lblRec->setGeometry(420, 6, 180, 36);
    m_lblRec->setAlignment(Qt::AlignCenter);
    m_lblRec->setStyleSheet(Theme::qssTextFlat());

    /* ---- 存储（右）---- */
    m_lblStore = new QLabel("-- / --", this);
    m_lblStore->setGeometry(760, 6, 248, 36);
    m_lblStore->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    /* 容量比时间低一级：24px（Secondary），不再用 12px 的 Small ——
     * 12px 在 27px 的顶栏里又小又糊，用户要求"更合理的大小"。 */
    QFont storeFont = font();
    storeFont.setPixelSize(Theme::Font::Secondary);
    m_lblStore->setFont(storeFont);
    m_lblStore->setStyleSheet(Theme::qssTextFlat());

    /* 原来这里有个 y=40 的"可用/总空间"提示，48px 高的栏里只露出 8px，
     * 屏幕上是一条被切掉的残条（也是"底下突起"观感的一部分）→ 不再创建。 */

    /* ---- 时间定时器：1 秒 ---- */
    m_timerClock = new QTimer(this);
    connect(m_timerClock, &QTimer::timeout, this, &UpWindow::updateTime);
    m_timerClock->start(1000);
    updateTime();                      // 立即显示，否则第一秒是空的

    /* ---- 存储定时器：5 秒 ---- */
    m_timerStore = new QTimer(this);
    connect(m_timerStore, &QTimer::timeout, this, &UpWindow::updateStorage);
    m_timerStore->start(5000);
    updateStorage();                   // 立即显示
}

UpWindow::~UpWindow()
{
}

void UpWindow::setRecording(bool on)
{
    if (on) {
        m_lblRec->setText("● REC");
        m_lblRec->setStyleSheet(Theme::qssTextFlat(Theme::AE::RecRed));
    } else {
        m_lblRec->setText("就绪");
        m_lblRec->setStyleSheet(Theme::qssTextFlat(Theme::AE::RecIdle));
    }
}

void UpWindow::updateTime()
{
    m_lblTime->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));
}

namespace {

/* KB → "73M" / "1.2G"。小于 1G 一律用 MB ——
 * 卡里剩 300M 的时候写 "0.3G" 不如写 "300M" 直观。 */
QString fmtKb(double kb)
{
    const double mb = kb / 1024.0;
    if (mb >= 1024.0)
        return QString("%1G").arg(mb / 1024.0, 0, 'f', 1);
    return QString("%1M").arg(qRound(mb));
}

} // namespace

void UpWindow::updateStorage()
{
    /* =========================================================================
     *  顶栏右侧 = 存储卡【剩余】空间
     * =========================================================================
     *  【为什么显示"剩余"而不是"已用 / 总量"】
     *    对录像机来说，"还能存多久"才是要看的数。
     *    而"已用/总量"在 63G 的卡上起步就是 "0.0G / 63G" —— 和 2026-10-29
     *    被砍掉的 UDISK 版本是同一个毛病：屏幕上一整年都是 0。
     *
     *  【历史】
     *    最初显示 UDISK 容量（6.1G 标成内存，被判定不合理）
     *    → 2026-10-29 改成读 /proc/meminfo 显示内存（已用/总量）
     *    → 2026-10-31 改成显示存储卡剩余（用户定：录像机该看存储）
     *
     *  【为什么走 storage_capacity 而不是直接 statvfs】
     *    卡拔掉后 /mnt/sdcard 目录还在（挂在 rootfs 上），直接 statvfs
     *    会拿到 eMMC 的容量，显示成"还有 500M" —— 看起来像卡快满了，
     *    实际是没卡。storage_capacity 内部先查 /proc/mounts 挡掉这种情况。
     * ====================================================================== */
    unsigned long long totalKb = 0;
    unsigned long long availKb = 0;

    if (storage_capacity(STORAGE_ROOT, &totalKb, &availKb) != 0) {
        m_lblStore->setText(QString::fromUtf8("无卡"));
        return;
    }

    m_lblStore->setText(QString::fromUtf8("可用 %1").arg(fmtKb((double)availKb)));
}
