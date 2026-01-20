#ifndef STDIN_TASK_H
#define STDIN_TASK_H

#include <rtsystem/core/task_helper.h>

#define DEFAULT_STDIN_TASK_PRIORITY 12

// Task configuration for stdin_task
// Use with task_create(arr, &stdin_task_config, &buf_size)
// init_arg: pointer to size_t buffer size
extern const task_config_t stdin_task_config;

#endif
