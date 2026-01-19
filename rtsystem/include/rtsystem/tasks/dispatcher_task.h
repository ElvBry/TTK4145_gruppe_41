#ifndef DISPATCHER_TASK_H
#define DISPATCHER_TASK_H
// TODO: create fifo_queue of commands the dispatcher should handle

#include "rtsystem/core/task_helper.h"
#include <stddef.h>

extern task_array_t g_dispatcher_task;

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

// Returns 0 on success, -1 on failure
int dispatcher_add_to_queue(cmd_t command);

// Returns 0 on success, -1 on failure
int dispatcher_task_init(const size_t cmd_buf_size, const int priority);

#endif