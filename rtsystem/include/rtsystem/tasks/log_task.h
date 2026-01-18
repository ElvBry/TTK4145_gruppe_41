#ifndef LOG_TASK_H
#define LOG_TASK_H

#include <signal.h>
#include <stddef.h>

// Shared shutdown flags (defined in main.c)
extern volatile sig_atomic_t g_running;
extern volatile sig_atomic_t g_sigint_count;

// Log task shutdown flag (defined in log_task.c)
// Set to 0 after all other tasks have stopped to allow final log drain
extern volatile int g_log_running;

// Event fd signaled when log_task exits (for poll-based waiting)
extern int g_log_done_fd;

// Initialize log queue and start log task thread
// queue_size: number of log messages the queue can hold
// priority: thread priority (0 for default, or SCHED_FIFO/RR priority 1-99)
// Returns 0 on success, -1 on error
int log_task_init(const size_t queue_size, const int priority);

// Signal log task to stop
static inline void log_task_stop(void) {
    g_log_running = 0;
}

// Wait for log task thread to finish
void log_task_join(void);

// Force cancel log task thread (use only if stuck)
void log_task_cancel(void);

// Cleanup log queue. Call after log_task_join.
void log_task_cleanup(void);

#endif
