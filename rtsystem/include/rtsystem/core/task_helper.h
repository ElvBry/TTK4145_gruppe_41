#ifndef TASK_HELPER_H
#define TASK_HELPER_H

#include <pthread.h>
#include <stddef.h>

typedef enum {
    TASK_STATE_INIT,
    TASK_STATE_RUNNING,
    TASK_STATE_STOPPING,
    TASK_STATE_STOPPED,
} task_state_t;

// Forward declarations for mutual reference
typedef struct task_handle task_handle_t;
typedef struct task_array task_array_t;

struct task_handle {
    pthread_t thread;
    int done_fd;
    volatile task_state_t state;
    const char* name;

    // Callbacks (NULL = use default / skip)
    void (*on_stop)(task_handle_t* self);
    void (*on_cleanup)(task_handle_t* self);

    // Reference to owning array (set on add, cleared on remove)
    task_array_t* array;
};

struct task_array {
    task_handle_t** slots;  // Fixed-size array, NULL = empty slot
    size_t capacity;
    pthread_mutex_t lock;
};

// Task handle functions
int task_handle_init(task_handle_t* handle, const char* name);
void task_handle_destroy(task_handle_t* handle);
void task_handle_mark_done(task_handle_t* handle);

// Single task operations (use callback if set, else default)
void task_stop(task_handle_t* handle);
void task_join(task_handle_t* handle);
void task_cancel(task_handle_t* handle);

// Task array functions
int task_array_init(task_array_t* arr, size_t capacity);
void task_array_destroy(task_array_t* arr);
int task_array_add(task_array_t* arr, task_handle_t* handle);
int task_array_remove(task_array_t* arr, task_handle_t* handle);

// Bulk operations on all tasks in array
void task_array_stop_all(task_array_t* arr);
int task_array_poll_all(task_array_t* arr, int sig_fd, int timeout_ms);
void task_array_cancel_all(task_array_t* arr);
void task_array_join_all(task_array_t* arr);  // Skips stuck tasks (checks done_fd first)
void task_array_handles_destroy_all(task_array_t* arr);

#endif
