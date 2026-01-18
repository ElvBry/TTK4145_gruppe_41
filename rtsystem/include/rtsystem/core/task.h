#ifndef TASK_H
#define TASK_H

#include <pthread.h>
#include <stdint.h>

typedef enum {
    TASK_STATE_INIT,
    TASK_STATE_RUNNING,
    TASK_STATE_STOPPING,
    TASK_STATE_STOPPED,
} task_state_t;

typedef struct {
    pthread_t thread;
    int done_fd;
    volatile task_state_t state;
    const char* name;
} task_handle_t;

// Initialize a task handle (creates done_fd)
// Returns 0 on success, -1 on error
int task_handle_init(task_handle_t* handle, const char* name);

// Destroy a task handle (closes done_fd)
void task_handle_destroy(task_handle_t* handle);

// Called by task thread when exiting - signals done_fd
void task_handle_mark_done(task_handle_t* handle);

// Wait for task to complete with timeout
// Returns 0 if task completed, -1 on timeout or error
int task_handle_wait(task_handle_t* handle, int timeout_ms);

// Non-blocking check if task is done
// Returns 1 if done, 0 if still running
int task_handle_is_done(task_handle_t* handle);

// Wait for multiple tasks to complete
// Returns number of tasks that completed, -1 on error
int task_handle_wait_multiple(task_handle_t** handles, size_t count, int timeout_ms);

#endif
