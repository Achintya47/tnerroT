/**
 * @brief : Minimal thread-safe logger. One mutex around the actual print,
 *          so N peer threads logging concurrently still produce whole,
 *          un-interleaved lines instead of garbled output. Timestamps are
 *          seconds-since-log_init() (a monotonic clock), not wall time —
 *          simpler, portable, and what you actually want for "how long
 *          into the download did this happen".
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#include "log.h"

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION log_lock_t;
#define LOG_LOCK_INIT(l) InitializeCriticalSection(l)
#define LOG_LOCK(l)      EnterCriticalSection(l)
#define LOG_UNLOCK(l)    LeaveCriticalSection(l)
#else
#include <pthread.h>
typedef pthread_mutex_t log_lock_t;
#define LOG_LOCK_INIT(l) pthread_mutex_init(l, NULL)
#define LOG_LOCK(l)      pthread_mutex_lock(l)
#define LOG_UNLOCK(l)    pthread_mutex_unlock(l)
#endif

static log_lock_t g_log_lock;
static volatile int g_log_initialized = 0;
static LogLevel g_log_level = LOG_INFO;

#ifdef _WIN32
static ULONGLONG g_start_tick;
#else
#include <time.h>
static struct timespec g_start_ts;
#endif

void log_init(void) {
    if (g_log_initialized)
        return;

    LOG_LOCK_INIT(&g_log_lock);

#ifdef _WIN32
    g_start_tick = GetTickCount64();
#else
    clock_gettime(CLOCK_MONOTONIC, &g_start_ts);
#endif

    g_log_initialized = 1;
}

void log_set_level(LogLevel level) {
    g_log_level = level;
}

static double elapsed_seconds(void) {
#ifdef _WIN32
    return (double)(GetTickCount64() - g_start_tick) / 1000.0;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - g_start_ts.tv_sec) +
           (double)(now.tv_nsec - g_start_ts.tv_nsec) / 1e9;
#endif
}

static const char* level_name(LogLevel level) {
    switch (level) {
        case LOG_ERROR: return "ERROR";
        case LOG_WARN:  return "WARN ";
        case LOG_INFO:  return "INFO ";
        case LOG_DEBUG: return "DEBUG";
        default:        return "?????";
    }
}

/* IP is already in network byte order (big-endian), i.e. byte[0] is the
   first octet on the wire — the same order dotted-quad notation reads in,
   so no ntohl()/winsock dependency needed just to print it. */
static void format_ip(uint32_t ip, char out[16]) {
    const unsigned char* b = (const unsigned char*)&ip;
    snprintf(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

void log_msg(LogLevel level, uint32_t peer_ip, uint16_t peer_port, const char* fmt, ...) {
    if (!g_log_initialized)
        log_init();

    if (level > g_log_level)
        return;

    char peer_buf[24];

    if (peer_ip != 0 || peer_port != 0) {
        char ip_str[16];
        format_ip(peer_ip, ip_str);
        snprintf(peer_buf, sizeof(peer_buf), "%s:%u", ip_str, peer_port);
    } else {
        snprintf(peer_buf, sizeof(peer_buf), "-");
    }

    LOG_LOCK(&g_log_lock);

    printf("[%8.3f] [%s] [%-21s] ", elapsed_seconds(), level_name(level), peer_buf);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
    fflush(stdout);

    LOG_UNLOCK(&g_log_lock);
}