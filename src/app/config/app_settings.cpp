// SPDX-License-Identifier: MIT
/** @file app_settings.cpp
 *  @brief settings.ini 读写实现
 */
#include "app_settings.h"
#include "log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>

#include <mutex>
#include <cstdio>

namespace {

const char *kPath = "/etc/dvr/settings.ini";

/* =============================================================================
 *  默认值表
 * =============================================================================
 *  两处用途，同一份数据：
 *    ① 文件不存在时，这就是初次生成的全部内容
 *    ② 文件存在但缺某个键时，用它补齐（可能是手改删掉了，也可能是旧版本文件）
 * ========================================================================== */
struct Def { const char *key; const char *val; };

const Def kDefaults[] = {
    { "general/version",      "1"    },   /* 配置格式版本，将来改键名要靠它做迁移 */
    { "record/autoStart",     "true" },   /* 上电后自动开始录像 */
    { "record/loopOverwrite", "true" },   /* 卡满时覆盖最旧的片段 */
    { "capture/width",        "1280" },   /* 采集宽（与摄像头协商）*/
    { "capture/height",       "720"  },   /* 采集高 */
    { "capture/fps",          "30"   },   /* 目标帧率 */

    /* 摄像头图像控制项（摄像头设置页「保存」写的就是这两个）。
     * 出厂默认：白平衡自动开、抗工频 50Hz（国内电网）——和这台 UVC 出厂值一致，
     * 所以首次生成配置时不会造成画面变化。 */
    { "camera/wbAuto",        "1"    },   /* 白平衡自动：1 自动 / 0 手动 */
    { "camera/powerLine",     "1"    },   /* 抗工频闪烁：0 关 / 1 50Hz / 2 60Hz */
};
const int kDefCount = int(sizeof(kDefaults) / sizeof(kDefaults[0]));

QMap<QString, QString> g_map;      /* 内存副本，键是 "section/key" */
bool g_loaded = false;

/* ★★ 加锁：
 *   `g_map` 有**两个写者**——UI 线程（用户点保存）和采集线程（换流/重连时写 capture/ 下的键）。
 *   `QMap` 是红黑树，一边插入一边遍历是 UB；`QString` 的隐式共享引用计数也不是原子的。
 *   用 recursive_mutex：因为 getter/setter 内部都会调 load()，load() 又调 saveToDisk()，
 *   同一线程会重复加锁。锁只在公开 API 入口加，内部函数假定锁已持有。 */
std::recursive_mutex g_mtx;
#define SETTINGS_LOCK() std::lock_guard<std::recursive_mutex> _lk(g_mtx)

/* =============================================================================
 *  落盘：QSaveFile → 临时文件 → fsync → rename
 * =============================================================================
 *  QMap 迭代本身有序（section 和 key 都按字典序），所以同样的内容
 *  每次写出来的字节完全一致，方便 diff 和校验。
 * ========================================================================== */
bool saveToDisk()
{
    QFileInfo fi(QString::fromLatin1(kPath));
    QDir().mkpath(fi.absolutePath());        /* /etc/dvr 首次可能不存在 */

    /* 按 section 分组 */
    QMap<QString, QStringList> groups;
    for (QMap<QString, QString>::const_iterator it = g_map.constBegin();
         it != g_map.constEnd(); ++it) {
        const int slash = it.key().indexOf(QLatin1Char('/'));
        const QString sec = (slash > 0) ? it.key().left(slash)
                                        : QStringLiteral("general");
        const QString k   = (slash > 0) ? it.key().mid(slash + 1) : it.key();
        groups[sec] << (k + QLatin1Char('=') + it.value());
    }

    QSaveFile f(QString::fromLatin1(kPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOGW("set", "打不开 %s：%s", kPath, f.errorString().toUtf8().constData());
        return false;
    }

    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    /* ★ 必须用 QString::fromUtf8 包一层。
     *   Qt5 的 QTextStream::operator<<(const char *) 内部走的是
     *   QLatin1String，把源码里的 UTF-8 字节当成 Latin-1 字符，
     *   再编码成 UTF-8 写出 → 中文注释变成乱码（双重编码）。
     *   键值部分都是 ASCII，不受影响，所以之前只有注释乱。 */
    ts << QString::fromUtf8("; DVR 设置 —— 手改之后重启程序生效\n")
       << QString::fromUtf8("; 缺失的键按代码里的默认值处理，不会报错\n");

    for (QMap<QString, QStringList>::const_iterator it = groups.constBegin();
         it != groups.constEnd(); ++it) {
        ts << "\n[" << it.key() << "]\n";
        for (int i = 0; i < it.value().size(); ++i)
            ts << it.value().at(i) << "\n";
    }
    ts.flush();

    if (!f.commit()) {
        LOGE("set", "落盘失败 %s：%s", kPath, f.errorString().toUtf8().constData());
        return false;
    }
    return true;
}

} // namespace

namespace AppSettings {

void load()
{
    SETTINGS_LOCK();

    if (g_loaded)
        return;

    QFileInfo fi(QString::fromLatin1(kPath));
    QDir().mkpath(fi.absolutePath());

    QFile f(QString::fromLatin1(kPath));
    const bool exists = f.exists();

    /* ---- ① 解析已有文件（容错：坏行跳过，不中断）---- */
    QString section;
    if (exists && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts.setCodec("UTF-8");
        while (!ts.atEnd()) {
            const QString line = ts.readLine().trimmed();
            if (line.isEmpty()
                || line.startsWith(QLatin1Char(';'))
                || line.startsWith(QLatin1Char('#')))
                continue;

            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                section = line.mid(1, line.length() - 2).trimmed();
                continue;
            }

            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)                    /* 没有等号 / 等号在行首：坏行 */
                continue;

            const QString k = line.left(eq).trimmed();
            const QString v = line.mid(eq + 1).trimmed();
            if (k.isEmpty())
                continue;

            g_map[section.isEmpty() ? k : (section + QLatin1Char('/') + k)] = v;
        }
        f.close();
    } else if (exists) {
        LOGW("set", "读不了 %s，按默认值继续", kPath);
    }

    /* ---- ② 补默认值 ---- */
    bool needWrite = !exists;
    for (int i = 0; i < kDefCount; ++i) {
        const QString k = QString::fromLatin1(kDefaults[i].key);
        if (!g_map.contains(k)) {
            g_map.insert(k, QString::fromLatin1(kDefaults[i].val));
            needWrite = true;
        }
    }

    g_loaded = true;

    if (needWrite)
        saveToDisk();

    LOGI("set", "%s %s（%d 项）", needWrite ? "已创建/补齐" : "已载入", kPath, g_map.size());
}

bool getBool(const QString &key, bool def)
{
    SETTINGS_LOCK();
    if (!g_loaded)
        load();

    const QMap<QString, QString>::const_iterator it = g_map.constFind(key);
    if (it == g_map.constEnd())
        return def;

    const QString v = it.value().trimmed().toLower();
    if (v == QLatin1String("true") || v == QLatin1String("1")
        || v == QLatin1String("yes") || v == QLatin1String("on"))
        return true;
    if (v == QLatin1String("false") || v == QLatin1String("0")
        || v == QLatin1String("no") || v == QLatin1String("off"))
        return false;
    return def;                       /* 认不出来的值：按默认处理 */
}

bool contains(const QString &key)
{
    SETTINGS_LOCK();
    if (!g_loaded)
        load();                       /* 和 getter 一致：没加载就先加载 */
    return g_map.contains(key);
}

int getInt(const QString &key, int def)
{
    SETTINGS_LOCK();
    if (!g_loaded)
        load();

    const QMap<QString, QString>::const_iterator it = g_map.constFind(key);
    if (it == g_map.constEnd())
        return def;

    bool ok = false;
    const int v = it.value().trimmed().toInt(&ok);
    return ok ? v : def;              /* 认不出来的值：按默认处理 */
}

QString getString(const QString &key, const QString &def)
{
    SETTINGS_LOCK();
    if (!g_loaded)
        load();
    return g_map.value(key, def);
}

bool setBool(const QString &key, bool v)
{
    return setString(key, v ? QStringLiteral("true") : QStringLiteral("false"));
}

bool setInt(const QString &key, int v)
{
    return setString(key, QString::number(v));
}

bool setString(const QString &key, const QString &v)
{
    SETTINGS_LOCK();

    if (!g_loaded)
        load();                       /* 防止没人调 load() 时把整个文件写没 */

    if (g_map.value(key) == v)
        return true;                  /* 值没变就不写盘（减少掉电窗口），视为成功 */

    g_map[key] = v;
    return saveToDisk();              /* ★ 落盘成败如实往上抛 —— 以前是 void，UI 只能瞎猜 */
}

} // namespace AppSettings
