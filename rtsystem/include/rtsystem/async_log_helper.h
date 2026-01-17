// NB: Only use together with the logging task
// like log_helper.h but with Async logging via fifo queue.
// Allows for timestamped logs that gets printed in chronological order

#ifndef ASYNC_LOG_HELPER_H
#define ASYNC_LOG_HELPER_H

#ifdef LOG_HELPER_H
    #error "Cannot use LOG_HELPER together with ASYNC_LOG_HELPER"
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <rtsystem/core/fifo_queue.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_NONE  4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

typedef struct {
    int level;
    char tag[32];
    char message[256];
    struct timespec timestamp;
} log_message_t;

// Global log queue (initialized by log_task)
extern fifo_queue_t g_log_queue;

extern volatile int g_log_running;

// Internal: Append log to queue (non-blocking)
#define ALOG(_level, _tag, _fmt, ...) \
    do { \
        if (_level >= LOG_LEVEL) { \
            if (!g_log_running) { \
                fprintf(stderr, "WARN: attempting to log while log_task not running [%s]\n", _tag); \
            } \
            log_message_t _msg; \
            _msg.level = _level; \
            strncpy(_msg.tag, _tag, sizeof(_msg.tag) - 1); \
            _msg.tag[sizeof(_msg.tag) - 1] = '\0'; \
            clock_gettime(CLOCK_REALTIME, &_msg.timestamp); \
            snprintf(_msg.message, sizeof(_msg.message), _fmt, ##__VA_ARGS__); \
            if (fifo_queue_send(&g_log_queue, &_msg) != 0) { \
                fprintf(stderr, "ERR: log queue full [%s]\n", _tag); \
            } \
        } \
    } while(0)

#define LOGD(_tag, _fmt, ...) ALOG(LOG_LEVEL_DEBUG, _tag, _fmt, ##__VA_ARGS__)
#define LOGI(_tag, _fmt, ...) ALOG(LOG_LEVEL_INFO,  _tag, _fmt, ##__VA_ARGS__)
#define LOGW(_tag, _fmt, ...) ALOG(LOG_LEVEL_WARN,  _tag, _fmt, ##__VA_ARGS__)
#define LOGE(_tag, _fmt, ...) ALOG(LOG_LEVEL_ERROR, _tag, _fmt, ##__VA_ARGS__)

#define LOGD_ERRNO(_tag, _fmt, ...) LOGD(_tag, _fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGI_ERRNO(_tag, _fmt, ...) LOGI(_tag, _fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGW_ERRNO(_tag, _fmt, ...) LOGW(_tag, _fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGE_ERRNO(_tag, _fmt, ...) LOGE(_tag, _fmt ": %s", ##__VA_ARGS__, strerror(errno))

#endif
