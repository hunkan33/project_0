// SPDX-License-Identifier: MIT
/** @file app_settings.h
 *  @brief settings.ini 的读写接口（原子写盘）
 */
#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <QString>

/* =============================================================================
 *  AppSettings —— 落盘的配置项（键值对）
 * =============================================================================
 *  【为什么不放在外观目录】（历史：src/ui/theme，现 ui/kit）
 *    Theme.h 开头写着「本文件只放常量 + QSS 字符串，不放任何逻辑」。
 *    主题目录一旦开始读写文件，它就不再是纯外观层，后面每加一个业务选项
 *    都会往那里塞 IO 代码。
 *  【现在住哪】src/infra/ ——
 *    配置是"程序状态"，与页面状态机（core/app_state）、布局表（core/page）并列，
 *    只是生命周期不同。
 *
 *  【文件位置】
 *    /etc/dvr/settings.ini
 *    rootfs 是 ext4 常驻挂载，1KB 的配置放这里没有压力；
 *    /mnt/UDISK 留给录像，将来「格式化 UDISK」不会连配置一起清掉。
 *
 *  【启动顺序】
 *    必须在创建任何 UI 之前调 load()，否则控件构造时拿不到真实初值，
 *    会先显示默认值、下一帧再跳成文件里的值，肉眼能看到闪一下。
 *
 *  【写盘策略】
 *    每次 set 都立即落盘，且走 QSaveFile（临时文件 → fsync → rename）。
 *    车载会直接断电，非原子写极可能留下 0 字节文件，
 *    下次启动读空 → 全部回默认值。
 * ========================================================================== */
namespace AppSettings {

/** @brief 启动时调一次：把 settings.ini 读进内存。
 *
 *  @note 文件不存在 → 用默认值建文件。
 *  @note 文件存在但缺键 → 缺的补默认值并写回（保持文件自洽）。
 *  @note 读失败 / 解析失败 → 按默认值继续跑，**不抛异常、不影响启动**。
 *        DVR 的第一要求是能起来，配置坏了不能变成起不来。
 */
void load();

/** @brief 读布尔值。
 *  @param key 配置键，如 @c "app/autoStart"。
 *  @param def 键不存在时的默认值。
 *  @return 读到的值；键不存在返回 @p def。
 */
bool    getBool  (const QString &key, bool def = false);

/** @brief 读整数。
 *  @param key 配置键，如 @c "capture/width"。
 *  @param def 键不存在时的默认值。
 *  @return 读到的值；键不存在返回 @p def。
 */
int     getInt   (const QString &key, int  def = 0);

/** @brief 读字符串。
 *  @param key 配置键。
 *  @param def 键不存在时的默认值。
 *  @return 读到的值；键不存在返回 @p def。
 */
QString getString(const QString &key, const QString &def = QString());

/** @brief 配置里是否存在这个键。
 *
 *  @param key 配置键，如 @c "camera/brightness"。
 *  @return true = 文件里/内存里有这个键；false = 从来没有过。
 *
 *  @note 和 getInt(key, def) 的区别：后者分不清"键不存在"和"键存在但值等于默认值"。
 *        开机恢复摄像头控制项时要用它 —— **用户没调过的项就不该碰硬件**，
 *        否则强行写默认值会把换过摄像头的画面推到奇怪状态。
 */
bool contains(const QString &key);

/** @brief 设布尔值并**立即**落盘。
 *  @param key 配置键。
 *  @param v   新值。
 *  @note 写失败只打日志，不影响程序运行（内存里的值仍然更新）。
 *  @note 落盘是原子写（tmp → fsync → rename），掉电不会留 0 字节文件。
 */
/** @brief 设布尔值并**立即**落盘。
 *  @param key 配置键，格式 @c "section/key"（如 @c "record/autoStart"）。
 *  @param v   新值。
 *  @return **落盘是否真的成功**（值没变也算成功）。
 *  @note  以前返回 void，UI 只能无条件显示"已保存"，
 *         而磁盘满/只读时那句"已保存"就是假的。 */
bool setBool  (const QString &key, bool v);

/** @brief 设整数并立即落盘。 @param key 配置键。 @param v 新值。 */
/** @brief 设整数并立即落盘。
 *  @param key 配置键，格式 @c "section/key"。
 *  @param v   新值。
 *  @return 落盘是否成功。 */
bool setInt   (const QString &key, int  v);

/** @brief 设字符串并立即落盘。 @param key 配置键。 @param v 新值。 */
/** @brief 设字符串并立即落盘。
 *  @param key 配置键，格式 @c "section/key"。
 *  @param v   新值。
 *  @return 落盘是否成功。 */
bool setString(const QString &key, const QString &v);

/* 调试用：打印出来方便 cat */

} // namespace AppSettings

#endif // APP_SETTINGS_H
