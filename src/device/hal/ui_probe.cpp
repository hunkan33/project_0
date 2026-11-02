// SPDX-License-Identifier: MIT
/** @file ui_probe.cpp
 *  @brief ui_probe.h 的实现
 */
#include "ui_probe.h"

#include "log.h"

#include <QApplication>
#include <QFile>
#include <QMouseEvent>
#include <QPoint>
#include <QRegExp>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <stdlib.h>

namespace {

constexpr const char *kCmdPath  = "/tmp/dvr_ui.cmd";
constexpr int         kPollMs   = 100;

/* 读一行命令。读到就删文件，返回 true */
bool takeCommand(QString *out)
{
    QFile f(QString::fromLatin1(kCmdPath));
    if (!f.exists())
        return false;
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QString line = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    f.remove();
    if (line.isEmpty())
        return false;
    if (out) *out = line;
    return true;
}

/* click X Y —— 找控件、post 按下+抬起 */
void doClick(int x, int y)
{
    const QPoint global(x, y);
    QWidget *w = QApplication::widgetAt(global);
    if (!w) {
        LOGW("probe", "点击 (%d,%d)：该坐标没有控件", x, y);
        return;
    }

    /* 日志里带上控件类型，脚本可以据此判断"点到了什么" */
    LOGI("probe", "点击 (%d,%d) → %s", x, y, w->metaObject()->className());

    const QPoint local = w->mapFromGlobal(global);
    QMouseEvent press(QEvent::MouseButtonPress, local, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);

    QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &release);
}

} // namespace

void ui_probe_start(void)
{
    const char *env = getenv("DVR_UI_PROBE");
    if (!env || env[0] != '1') {
        return;                       /* 生产模式：什么都不做 */
    }

    QTimer *t = new QTimer(QApplication::instance());
    QObject::connect(t, &QTimer::timeout, []() {
        QString cmd;
        if (!takeCommand(&cmd))
            return;

        /* 目前只支持 click X Y；将来可以加 key / swipe */
        const QStringList parts = cmd.split(QRegExp("[\\s,]+"), QString::SkipEmptyParts);
        if (parts.size() == 3 && parts.at(0).compare("click", Qt::CaseInsensitive) == 0) {
            doClick(parts.at(1).toInt(), parts.at(2).toInt());
        } else {
            LOGW("probe", "看不懂的命令：%s", qPrintable(cmd));
        }
    });
    t->start(kPollMs);

    LOGI("probe", "UI 探针已启动（写 %s 形如：click 512 300）", kCmdPath);
}
