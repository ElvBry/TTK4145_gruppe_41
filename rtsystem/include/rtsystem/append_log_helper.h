// NB: Only use together with the logging task
// like log_helper.h but with Async logging via message queue.
// Allows for timestamped logs that gets printed in chronological order after 

#ifndef ASYNC_LOG_HELPER_H
#define ASYNC_LOG_HELPER_H

#ifdef LOG_HELPER_H
    #error "Cannot use LOG_HELPER together with ASYNC_LOG_HELPER"
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <mqueue.h>
#include <stdarg.h>
#include <time.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_NONE  4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#ifdef LOG_NO_COLOR
    #define LOG_COLOR_RED    ""
    #define LOG_COLOR_GREEN  ""
    #define LOG_COLOR_YELLOW ""
    #define LOG_COLOR_CYAN   ""
    #define LOG_COLOR_RESET  ""
#else
    #define LOG_COLOR_RED    "\033[31m"
    #define LOG_COLOR_GREEN  "\033[32m"
    #define LOG_COLOR_YELLOW "\033[33m"
    #define LOG_COLOR_CYAN   "\033[36m"
    #define LOG_COLOR_RESET  "\033[0m"
#endif

typedef struct {
    int level;
    char tag[32];
    char message[256];
    struct timespec timestamp;  // Captured when macro is called
} log_message_t;

// Global log queue (initialized by system)
extern mqd_t g_log_queue;

// Internal: Append log to queue (non-blocking)
#define ALOG(level, tag, fmt, ...) \
    do { \
        if (level >= LOG_LEVEL) { \
            log_message_t _msg; \
            _msg.level = level; \
            strncpy(_msg.tag, tag, sizeof(_msg.tag) - 1); \
            _msg.tag[sizeof(_msg.tag) - 1] = '\0'; \
            clock_gettime(CLOCK_REALTIME, &_msg.timestamp); \
            snprintf(_msg.message, sizeof(_msg.message), fmt, ##__VA_ARGS__); \
            mq_send(g_log_queue, (char*)&_msg, sizeof(_msg), level); \
        } \
    } while(0)

#define LOGD(tag, fmt, ...) ALOG(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) ALOG(LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) ALOG(LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) ALOG(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)

#define LOGD_ERRNO(tag, fmt, ...) LOGD(tag, fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGI_ERRNO(tag, fmt, ...) LOGI(tag, fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGW_ERRNO(tag, fmt, ...) LOGW(tag, fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGE_ERRNO(tag, fmt, ...) LOGE(tag, fmt ": %s", ##__VA_ARGS__, strerror(errno))

#endif