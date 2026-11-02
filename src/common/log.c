// SPDX-License-Identifier: MIT
/** @file log.c
 *  @brief 统一日志实现
 */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strcasecmp */
#include <time.h>

/* ---- 全局状态 ----
 * 三个都是"写一次读多次"，不加锁：
 *   g_min 只有 log_init 改（main 里调一次，早于任何线程）
 *   g_t0  也是 log_init 里定
 *   g_inited 是惰性初始化的标志
 * 如果真的有人在运行期调 log_init 改级别，那是未定义行为 —— 但本工程不会。 */
static LogLevel       g_min    = LOG_LEVEL_INFO;
static struct timespec g_t0;
static int            g_inited = 0;

static void ensure_init(void)
{
    if (g_inited)
        return;
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    g_inited = 1;
}

static const char *level_name(LogLevel lv)
{
    switch (lv) {
    case LOG_LEVEL_DEBUG: return "debug";
    case LOG_LEVEL_INFO:  return "info";
    case LOG_LEVEL_WARN:  return "warn";
    case LOG_LEVEL_ERROR: return "error";
    }
    return "?";
}

static char level_char(LogLevel lv)
{
    switch (lv) {
    case LOG_LEVEL_DEBUG: return 'D';
    case LOG_LEVEL_INFO:  return 'I';
    case LOG_LEVEL_WARN:  return 'W';
    case LOG_LEVEL_ERROR: return 'E';
    }
    return '?';
}

static LogLevel level_from_env(void)
{
    const char *e = getenv("DVR_LOG_LEVEL");
    if (!e || !e[0])
        return LOG_LEVEL_INFO;
    if (strcasecmp(e, "debug") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(e, "info")  == 0) return LOG_LEVEL_INFO;
    if (strcasecmp(e, "warn")  == 0) return LOG_LEVEL_WARN;
    if (strcasecmp(e, "error") == 0) return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

void log_init(LogLevel min_level)
{
    ensure_init();

    const char *env = getenv("DVR_LOG_LEVEL");
    g_min = (env && env[0]) ? level_from_env() : min_level;

    /* 打一行绝对时间锚点。之后全部用单调秒数 ——
     * 这样即使系统时间被 NTP 改动，日志里的间隔仍然是正确的。 */
    const time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    fprintf(stderr, "[log] 启动于 %s，级别 %s%s\n",
            ts, level_name(g_min), (env && env[0]) ? "（环境变量指定）" : "");
}

void log_write(LogLevel lv, const char *mod, const char *fmt, ...)
{
    ensure_init();

    if (lv < g_min)
        return;

    /* 单调秒数（相对 log_init 那一刻） */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const double sec = (double)(now.tv_sec  - g_t0.tv_sec)
                     + (double)(now.tv_nsec - g_t0.tv_nsec) / 1e9;

    /* 先格式化到栈上，再一次性 fprintf ——
     * 这是"多线程不会断行"的关键：分多次 fprintf 才会互相穿插。 */
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    fprintf(stderr, "[%7.3f][%-5s][%c] %s\n",
            sec, (mod && mod[0]) ? mod : "-", level_char(lv), msg);
}
