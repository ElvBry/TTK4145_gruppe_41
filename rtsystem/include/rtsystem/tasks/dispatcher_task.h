#ifndef DISPATCHER_TASK_H
#define DISPATCHER_TASK_H

#include <rtsystem/core/task_helper.h>
#include <stddef.h>

typedef enum {
    UDP,
    TCP,
    ECHO,
    HELP,
    NIL,
} cmd_type_t;

typedef struct cmd cmd_t;

struct cmd {
    int argc;
    char **argv;
    cmd_type_t cmd_type;
};

#define DEFAULT_DISPATCHER_TASK_PRIORITY 40

// Task configuration for dispatcher_task
// Use with task_create(arr, &dispatcher_task_config, &queue_size)
// init_arg: pointer to size_t queue size
extern const task_config_t dispatcher_task_config;

// global way to add command to the dispatcher queue
// Returns 0 on success, -1 on failure
int dispatcher_add_to_queue(cmd_t command);

#endif