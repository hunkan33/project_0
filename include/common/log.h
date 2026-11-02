// SPDX-License-Identifier: MIT
/** @file log.h
 *  @brief 统一日志：单调时间戳 + 模块名 + 级别
 *
 *  【解决什么问题】
 *    之前 51 个文件各自 `fprintf(stderr, "[cap] xxx\n")`，问题有三个：
 *      ① 没有时间戳 —— 排查"卡了 200ms"这类问题全靠猜顺序
 *      ② 没有级别 —— 想临时关掉调试输出只能改代码重编
 *      ③ 格式各异 —— 有的带 `[cap]` 前缀，有的不带
 *
 *  【时间戳用 CLOCK_MONOTONIC，不是 wall clock】
 *    DVR 要查"视频帧到显示之间隔了多久"，需要的是**单调**时间差。
 *    wall clock 会被 NTP / 用户改时间影响，一改日志就出现负间隔或者跳变。
 *    所以：只在 log_init 时打一行 wall clock 锚点，之后全部用单调秒数。
 *
 *  【输出格式】
 *  @verbatim
 *    [  12.345][cap  ][I] 生效格式 1920x1080 @30fps
 *     ↑单调秒     ↑模块  ↑级别
 *  @endverbatim
 *    级别字母：D 调试 / I 信息 / W 警告 / E 错误
 *
 *  【怎么临时调级别（不用重编）】
 *  @verbatim
 *    DVR_LOG_LEVEL=debug ./work        # 打开调试
 *    ./work                            # 默认 info
 *  @endverbatim
 *
 *  【线程安全】
 *    每条日志**一次 fprintf 写完**，多线程并发不会断行。
 *    （glibc 的 stdio 对单次调用加锁；分多次 printf 才会互相穿插。）
 *    不保证跨行顺序 —— 两个线程同时打，谁先谁后不确定，但每行都是完整的。
 * ========================================================================== */
#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 日志级别，数值越大越严重。 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,   /**< 调试细节，默认不输出 */
    LOG_LEVEL_INFO,        /**< 正常流程的关键节点 */
    LOG_LEVEL_WARN,        /**< 可以继续，但偏离预期 */
    LOG_LEVEL_ERROR        /**< 出错，功能可能已失效 */
} LogLevel;

/** @brief 初始化日志（在 main 里尽早调用一次）。
 *
 *  @param min_level 默认最低级别；**环境变量 DVR_LOG_LEVEL 会覆盖它**
 *                   （debug / info / warn / error，大小写不敏感）。
 *
 *  @note 会打一行"启动于 <wall clock>"作为绝对时间锚点，之后全部用单调时刻。
 *  @note 本函数不是必须调用 —— 没调的话第一次打日志时会自动以 INFO 初始化，
 *        但那样就没有 wall clock 锚点。
 */
void log_init(LogLevel min_level);

/** @brief 写一条日志（一般不用直接调，用下面的 LOGx 宏）。
 *
 *  @param lv  级别；低于 log_init() 设的最低级别会被丢弃。
 *  @param mod 模块名（≤5 字符），输出在第二个方括号里。
 *  @param fmt printf 格式串；**末尾不用带 \n**，log_write 会加。
 */
void log_write(LogLevel lv, const char *mod, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/** @brief 调试级。@param mod 模块名（≤5 字符，如 "cap"）。 */
#define LOGD(mod, ...) log_write(LOG_LEVEL_DEBUG, mod, __VA_ARGS__)
/** @brief 信息级。 */
#define LOGI(mod, ...) log_write(LOG_LEVEL_INFO,  mod, __VA_ARGS__)
/** @brief 警告级。 */
#define LOGW(mod, ...) log_write(LOG_LEVEL_WARN,  mod, __VA_ARGS__)
/** @brief 错误级。 */
#define LOGE(mod, ...) log_write(LOG_LEVEL_ERROR, mod, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
