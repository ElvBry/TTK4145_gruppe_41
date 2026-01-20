#ifndef EXAMPLE_WORKER_TASK_H
#define EXAMPLE_WORKER_TASK_H

#define DEFAULT_WORK_EXAMPLE_PRIORITY 20


#include <rtsystem/core/task_helper.h>

typedef struct {
    size_t time_to_live_ms;
    size_t msg_send_period_ms;
    char * message;
    size_t msg_len;
} worker_data_t;

extern const task_config_t worker_task_config;


#endif