#ifndef TASK_HELPER_H
#define TASK_HELPER_H

#include <pthread.h>
#include <stddef.h>

typedef enum {
    TASK_STATE_INIT,     // Handle created, thread not yet running
    TASK_STATE_RUNNING,  // Task is actively running
    TASK_STATE_STOPPING, // Stop requested, task is cleaning up
    TASK_STATE_STOPPED,  // Task has finished (done_fd signaled)
} task_state_t;

typedef struct task_handle task_handle_t;
typedef struct task_array  task_array_t;
typedef struct task_config task_config_t;

struct task_config {
    const int priority;   // SCHED_FIFO priority (0 = inherit)
    void *(*entry)(task_handle_t *self);
    // Called synchronously inside task_create before the thread starts.
    // Return 0 on success, -1 to abort creation.
    int  (*on_init)(task_handle_t *self, void *init_arg);
    // Called on_stop to request a graceful shutdown.
    // If NULL, the default sets state = TASK_STATE_STOPPING.
    void (*on_stop)(task_handle_t *self);
    // Called during handle destruction to release resources.
    void (*on_cleanup)(task_handle_t *self);
};

struct task_handle {
    const task_config_t  *config;
    const char           *name;
    pthread_t             thread;
    int                   done_fd;        // eventfd signaled when task finishes
    volatile task_state_t state;
    void                 *task_resources;
    task_array_t         *array;          // Back-reference to owning array
};

struct task_array {
    task_handle_t **slots;
    size_t          capacity;
    pthread_mutex_t lock;
};

// Create a task from config, add to array, and start its thread.
// on_init is called synchronously before the thread starts.
// Returns handle on success, NULL on failure.
task_handle_t *task_create(task_array_t *arr, const task_config_t *config,
                           void *init_arg, const char *name);

// Signal that the task has finished. Call at the end of the entry function.
void task_handle_mark_done(task_handle_t *handle);

// Initialize a task array with a fixed capacity.
// Returns 0 on success, -1 on failure.
int  task_array_init(task_array_t *arr, size_t capacity);

// Free the array's slot storage and destroy its mutex.
// Call after task_array_destroy_all.
void task_array_destroy(task_array_t *arr);

// Send a stop signal to every task in the array.
void task_array_stop_all(task_array_t *arr);

// Block until all tasks finish, timeout_ms elapses, or sig_fd fires.
// Returns >= 0 (tasks completed), -1 (timeout), -2 (sig_fd), -3 (poll error).
int  task_array_poll_all(task_array_t *arr, int sig_fd, int timeout_ms);

// pthread_cancel every task in the array (last resort).
void task_array_cancel_all(task_array_t *arr);

// pthread_join every task that has signalled done_fd.
void task_array_join_all(task_array_t *arr);

// Call on_cleanup and free every handle in the array.
// Call after task_array_join_all.
void task_array_destroy_all(task_array_t *arr);

#endif
