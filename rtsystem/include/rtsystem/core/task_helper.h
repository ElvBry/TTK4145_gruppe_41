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
typedef struct task_array task_array_t;
typedef struct task_config task_config_t;

struct task_config {
    const char* name;
    // Scheduler priority (0 = inherit, >0 = SCHED_FIFO)
    int priority;
    void* (*entry)(task_handle_t* self);
    // Called by task_create after handle setup, before thread starts
    // Use to allocate resources into self->user_data
    // Return 0 on success, -1 on failure
    int (*on_init)(task_handle_t* self, void* init_arg);
    // Called by task_stop to signal the task to stop
    // If NULL, default behavior sets state = TASK_STATE_STOPPING
    void (*on_stop)(task_handle_t* self);
    // Called during handle destruction to free resources
    // Use to free self->user_data and any other resources
    void (*on_cleanup)(task_handle_t* self);
};

struct task_handle {
    const task_config_t* config;
    pthread_t thread;
    int done_fd;                  // eventfd signaled when task finishes
    volatile task_state_t state;
    void* task_resources;         
    task_array_t* array;          // Back-reference to owning array (or NULL)
};

struct task_array {
    task_handle_t** slots;   // Fixed-size array of handle pointers (NULL = empty)
    size_t capacity;         // Maximum number of tasks
    pthread_mutex_t lock;    // Mutex with priority inheritance
};

// Create a new task from config, add to array, and start the thread
// Calls config->on_init(handle, init_arg) if on_init is set
// Returns handle on success, NULL on failure
task_handle_t* task_create(task_array_t* arr, const task_config_t* config, void* init_arg);

// Mark task as done and signal done_fd
// Call this at the end of your entry function before returning
void task_handle_mark_done(task_handle_t* handle);

// Destroy a handle: call on_cleanup, close done_fd, free handle
// Automatically removes from array if still registered
void task_handle_destroy(task_handle_t* handle);

// =============================================================================
// Single Task Operations
// =============================================================================

// Signal a task to stop (calls on_stop or sets state to STOPPING)
void task_stop(task_handle_t* handle);

// Wait for task thread to finish (pthread_join)
void task_join(task_handle_t* handle);

// Force cancel task thread (pthread_cancel) - use as last resort
void task_cancel(task_handle_t* handle);

// =============================================================================
// Task Array Management
// =============================================================================

// Initialize array with given capacity
// Returns 0 on success, -1 on failure
int task_array_init(task_array_t* arr, size_t capacity);

// Destroy array (frees slots, destroys mutex)
// Does NOT destroy handles - call task_array_destroy_all first
void task_array_destroy(task_array_t* arr);

// Add handle to array (finds first empty slot)
// Returns 0 on success, -1 if array is full
int task_array_add(task_array_t* arr, task_handle_t* handle);

// Remove handle from array
// Returns 0 on success, -1 if not found
int task_array_remove(task_array_t* arr, task_handle_t* handle);

// Get number of active tasks in array
size_t task_array_count(task_array_t* arr);

// =============================================================================
// Bulk Operations
// =============================================================================

// Send stop signal to all tasks in array
void task_array_stop_all(task_array_t* arr);

// Poll all task done_fds with a timeout
// Also monitors sig_fd for forced shutdown (e.g., second SIGINT)
// Returns:
//   >= 0 : Number of tasks that completed
//   -1   : Timeout expired (no tasks completed)
//   -2   : sig_fd received signal (force shutdown requested)
//   -3   : poll() error (logged internally)
int task_array_poll_all(task_array_t* arr, int sig_fd, int timeout_ms);

// Force cancel all tasks (pthread_cancel)
void task_array_cancel_all(task_array_t* arr);

// Join all tasks that have signaled done_fd
// Skips tasks that haven't finished (logs warning)
void task_array_join_all(task_array_t* arr);

// Destroy all handles in array (calls on_cleanup, frees handles)
// Call after task_array_join_all
void task_array_destroy_all(task_array_t* arr);

// =============================================================================
// Dynamic Task Cleanup (for worker tasks)
// =============================================================================

// Reap finished tasks: join and destroy any tasks that have signaled done_fd
// Used periodically for dynamic worker tasks that self-terminate
// Returns number of tasks reaped
int task_array_reap_finished(task_array_t* arr);

#endif
