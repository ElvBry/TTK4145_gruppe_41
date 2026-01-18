#ifndef STDIN_TASK_H
#define STDIN_TASK_H

#include <stddef.h>
#include <rtsystem/core/task.h>

typedef struct {
    size_t buf_size;
} stdin_arg_t;

// Task handle for stdin_task (use g_stdin_handle.done_fd for polling)
extern task_handle_t g_stdin_handle;

// Allocates input buffer and initializes stdin_task for user input.
// Stops when g_running flag is set to 0 or stdin_task_stop() is called
// buf_size: largest input read from stdin
// priority: thread priority (0 for default, or SCHED_FIFO/RR priority 1-99)
// Returns:  0 on success, -1 on failure
int stdin_task_init(const size_t buf_size, const int priority);

// Signal stdin task to stop (non-blocking)
void stdin_task_stop(void);

// Wait for stdin task thread to finish
void stdin_task_join(void);

// Force cancel stdin task thread (only use if necessary)
void stdin_task_cancel(void);

// Clean up task handle resources
void stdin_task_destroy(void);

#endif
