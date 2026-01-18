#ifndef STDIN_TASK_H
#define STDIN_TASK_H

#include <stddef.h>
#include <rtsystem/core/task_helper.h>

// Task array for stdin tasks (capacity 1, only one stdin task allowed)
extern task_array_t g_stdin_tasks;

// Allocates input buffer and initializes stdin_task for user input.
// Stops when g_running flag is set to 0 or task_stop() is called
// buf_size: largest input read from stdin
// priority: thread priority (0 for default, or SCHED_FIFO/RR priority 1-99)
// Returns:  0 on success, -1 on failure
int stdin_task_init(const size_t buf_size, const int priority);

#endif
