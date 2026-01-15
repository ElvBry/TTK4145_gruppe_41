// Custom logger, inspired by ESP-IDF esp_log component

/* Usage example:
#define LOG_LEVEL LOG_LEVEL_DEBUG // Select different level to omit certain messages during compilation
#include "log_helper.h"

static const char *TAG = "main";

int main(void) {
    int count = 42;
    const char *name = "test";

    LOGD(TAG, "starting application");
    LOGI(TAG, "processing %d items", count);
    LOGW(TAG, "item '%s' is deprecated", name);
    LOGE(TAG, "failed to open file: %s", "config.txt");
    printf("normal message\n");
    return 0;
}
*/ 

#ifndef LOG_HELPER_H
#define LOG_HELPER_H

#include <stdio.h>
#include <string.h>
#include <errno.h>

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

#define LOG(level, tag, fmt, ...) \
    do { if (level >= LOG_LEVEL) \
        fprintf(stderr, "%s%s %s: " fmt LOG_COLOR_RESET "\n", \
            (const char*[]){LOG_COLOR_CYAN, LOG_COLOR_GREEN, LOG_COLOR_YELLOW, LOG_COLOR_RED}[level], \
            (const char*[]){"D", "I", "W", "E"}[level], \
            tag, ##__VA_ARGS__); \
    } while(0)

#define LOGD(tag, fmt, ...) LOG(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) LOG(LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) LOG(LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) LOG(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)

#define LOGD_ERRNO(tag, fmt, ...) LOGD(tag, fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGI_ERRNO(tag, fmt, ...) LOGI(tag, fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGW_ERRNO(tag, fmt, ...) LOGW(tag, fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define LOGE_ERRNO(tag, fmt, ...) LOGE(tag, fmt ": %s", ##__VA_ARGS__, strerror(errno))

#endif