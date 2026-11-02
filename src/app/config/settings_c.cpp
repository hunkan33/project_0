// SPDX-License-Identifier: MIT
/** @file settings_c.cpp
 *  @brief settings_c.h 的实现 —— 唯一的 Qt 转换点
 */
#include "settings_c.h"
#include "app_settings.h"

#include <QString>

extern "C" {

int settings_get_int(const char *key, int def)
{
    if (!key) return def;
    return AppSettings::getInt(QString::fromLatin1(key), def);
}

int settings_get_bool(const char *key, int def)
{
    if (!key) return def;
    return AppSettings::getBool(QString::fromLatin1(key), def != 0) ? 1 : 0;
}

int settings_set_int(const char *key, int v)
{
    if (!key) return -1;
    return AppSettings::setInt(QString::fromLatin1(key), v) ? 0 : -1;
}

int settings_set_bool(const char *key, int v)
{
    if (!key) return -1;
    return AppSettings::setBool(QString::fromLatin1(key), v != 0) ? 0 : -1;
}

int settings_has(const char *key)
{
    if (!key) return 0;
    return AppSettings::contains(QString::fromLatin1(key)) ? 1 : 0;
}

} // extern "C"
