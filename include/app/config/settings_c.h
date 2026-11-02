// SPDX-License-Identifier: MIT
/** @file settings_c.h
 *  @brief 配置读写的 **C 友好接口**（无 Qt）
 *
 *  【为什么还要这一层】
 *    `app_settings.h` 的签名全是 `const QString &`，平台层（platform/）一旦调用它，
 *    就把 QString 拖进来了，后果实际发生过：
 *    `capture_thread.cpp:112` 出现 `QString::fromLatin1(it.key)`。
 *
 *    所以配置能力对外提供两个面：
 *      · `app_settings.h`（Qt 面）—— 给 ui/、core/ 用，支持中文键、带默认值
 *      · `settings_c.h`（C 面，本文件）—— 给 platform/ 用，**只认 const char\***
 *
 *  【约定】
 *    · 键名只能是 ASCII（`"camera/wbAuto"` 这种），不允许中文键
 *    · get 系列找不到键时返回 def，不报错
 *    · set 系列是整个 ini 原子重写（内部走 AppSettings::setInt）
 *
 *  @note 本头文件**不许** include 任何 Qt 头，否则这层就白做了。
 */
#ifndef _SETTINGS_C_H_
#define _SETTINGS_C_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 读整型。
 *  @param key 配置键（ASCII，形如 @c "camera/wbAuto"）。
 *  @param def 键不存在时的返回值。
 *  @return 读到的值；键不存在返回 @p def。 */
int settings_get_int(const char *key, int def);

/** @brief 读布尔（ini 里是 true/false 或 1/0）。
 *  @param key 配置键（ASCII）。
 *  @param def 键不存在时的返回值（0/1）。
 *  @return 读到的值；键不存在返回 @p def。 */
int settings_get_bool(const char *key, int def);

/** @brief 写整型（原子落盘）。
 *  @param key 配置键（ASCII）。
 *  @param v   新值。
 *  @return 0 成功；-1 失败（key 为空或磁盘写入失败）。 */
int settings_set_int(const char *key, int v);

/** @brief 写布尔（原子落盘）。
 *  @param key 配置键（ASCII）。
 *  @param v   新值（0/1）。
 *  @return 0 成功；-1 失败。 */
int settings_set_bool(const char *key, int v);

/** @brief 键是否存在（用来区分"从没配过"和"值恰好等于默认值"）。
 *  @param key 配置键（ASCII）。
 *  @return 1 存在；0 不存在（含 key 为空）。 */
int settings_has(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* _SETTINGS_C_H_ */
