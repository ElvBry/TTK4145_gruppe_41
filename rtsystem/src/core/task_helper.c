#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/core/task_helper.h>
#include <rtsystem/async_log_helper.h>

static const char *TAG = "task_helper";

int task_handle_init(task_handle_t* handle, const char* name) {
    handle->thread = 0;
    handle->state = TASK_STATE_INIT;
    handle->name = name;
    handle->on_stop = NULL;
    handle->on_cleanup = NULL;
    handle->array = NULL;

    handle->done_fd = eventfd(0, EFD_NONBLOCK);
    if (handle->done_fd == -1) {
        LOGE_ERRNO(TAG, "eventfd failed for task '%s': ", name);
        return -1;
    }

    LOGD(TAG, "initialized handle for task '%s'", name);
    return 0;
}

void task_handle_destroy(task_handle_t* handle) {
    // Remove from array if still registered
    if (handle->array != NULL) {
        task_array_remove(handle->array, handle);
    }

    if (handle->done_fd != -1) {
        close(handle->done_fd);
        handle->done_fd = -1;
    }
    handle->state = TASK_STATE_STOPPED;
    LOGD(TAG, "destroyed handle for task '%s'", handle->name);
}

void task_handle_mark_done(task_handle_t* handle) {
    handle->state = TASK_STATE_STOPPED;
    uint64_t done = 1;
    write(handle->done_fd, &done, sizeof(done));
    LOGD(TAG, "task '%s' marked done", handle->name);
}

void task_stop(task_handle_t* handle) {
    if (handle->on_stop != NULL) {
        handle->on_stop(handle);
    } else {
        handle->state = TASK_STATE_STOPPING;
    }
    LOGD(TAG, "stop signal sent to task '%s'", handle->name);
}

void task_join(task_handle_t* handle) {
    pthread_join(handle->thread, NULL);
    LOGD(TAG, "joined task '%s'", handle->name);
}

void task_cancel(task_handle_t* handle) {
    pthread_cancel(handle->thread);
    LOGW(TAG, "cancelled task '%s'", handle->name);
}

int task_array_init(task_array_t* arr, size_t capacity) {
    arr->slots = calloc(capacity, sizeof(task_handle_t*));
    if (arr->slots == NULL) {
        LOGE(TAG, "failed to allocate task array of capacity %zu", capacity);
        return -1;
    }

    arr->capacity = capacity;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&arr->lock, &attr);
    pthread_mutexattr_destroy(&attr);

    LOGD(TAG, "initialized task array with capacity %zu", capacity);
    return 0;
}

void task_array_destroy(task_array_t* arr) {
    pthread_mutex_destroy(&arr->lock);
    free(arr->slots);
    arr->slots = NULL;
    arr->capacity = 0;
    LOGD(TAG, "destroyed task array");
}

int task_array_add(task_array_t* arr, task_handle_t* handle) {
    pthread_mutex_lock(&arr->lock);

    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] == NULL) {
            arr->slots[i] = handle;
            handle->array = arr;
            pthread_mutex_unlock(&arr->lock);
            LOGD(TAG, "added task '%s' to array at slot %zu", handle->name, i);
            return 0;
        }
    }

    pthread_mutex_unlock(&arr->lock);
    LOGE(TAG, "task array full, cannot add task '%s'", handle->name);
    return -1;
}

int task_array_remove(task_array_t* arr, task_handle_t* handle) {
    pthread_mutex_lock(&arr->lock);

    // Iterate over whole array to remove task. Efficient for small arrays
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] == handle) {
            arr->slots[i] = NULL;
            handle->array = NULL;
            pthread_mutex_unlock(&arr->lock);
            LOGD(TAG, "removed task '%s' from array slot %zu", handle->name, i);
            return 0;
        }
    }

    pthread_mutex_unlock(&arr->lock);
    LOGW(TAG, "task '%s' not found in array", handle->name);
    return -1;
}

void task_array_stop_all(task_array_t* arr) {
    pthread_mutex_lock(&arr->lock);
    size_t count = 0;
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            task_stop(arr->slots[i]);
            count++;
        }
    }
    pthread_mutex_unlock(&arr->lock);
    LOGD(TAG, "sent stop signal to %zu task(s) in array", count);
}

int task_array_poll_all(task_array_t* arr, int sig_fd, int timeout_ms) {
    pthread_mutex_lock(&arr->lock);

    // Count active tasks
    size_t count = 0;
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            count++;
        }
    }

    if (count == 0) {
        pthread_mutex_unlock(&arr->lock);
        return 0;
    }

    // Build poll array: task done_fds + optional sig_fd
    size_t nfds = count + (sig_fd >= 0 ? 1 : 0);
    struct pollfd* fds = malloc(nfds * sizeof(struct pollfd));
    if (fds == NULL) {
        pthread_mutex_unlock(&arr->lock);
        LOGE(TAG, "failed to allocate pollfd array");
        return -1;
    }

    size_t fd_idx = 0;
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            fds[fd_idx].fd = arr->slots[i]->done_fd;
            fds[fd_idx].events = POLLIN;
            fd_idx++;
        }
    }

    if (sig_fd >= 0) {
        fds[fd_idx].fd = sig_fd;
        fds[fd_idx].events = POLLIN;
    }

    pthread_mutex_unlock(&arr->lock);

    int ret = poll(fds, nfds, timeout_ms);

    // Check if signal fd triggered (force shutdown)
    if (sig_fd >= 0 && ret > 0 && (fds[nfds - 1].revents & POLLIN)) {
        free(fds);
        return -2;  // Signal received
    }

    // Count completed tasks
    int completed = 0;
    if (ret > 0) {
        for (size_t i = 0; i < count; i++) {
            if (fds[i].revents & POLLIN) {
                completed++;
            }
        }
    }

    free(fds);
    return (ret == 0) ? -1 : completed;  // -1 on timeout
}

void task_array_cancel_all(task_array_t* arr) {
    pthread_mutex_lock(&arr->lock);
    size_t count = 0;
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            task_cancel(arr->slots[i]);
            count++;
        }
    }
    pthread_mutex_unlock(&arr->lock);
    LOGW(TAG, "cancelled %zu task(s) in array", count);
}

void task_array_join_all(task_array_t* arr) {
    pthread_mutex_lock(&arr->lock);
    size_t joined = 0;
    size_t skipped = 0;
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            task_handle_t* handle = arr->slots[i];

            // Check if task is ready to join (done_fd readable)
            struct pollfd pfd = { .fd = handle->done_fd, .events = POLLIN };
            if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
                task_join(handle);
                joined++;
            } else {
                LOGW(TAG, "task '%s' stuck, skipping join", handle->name);
                skipped++;
            }
        }
    }
    pthread_mutex_unlock(&arr->lock);
    if (skipped > 0) {
        LOGW(TAG, "joined %zu task(s), skipped %zu stuck task(s)", joined, skipped);
    } else {
        LOGD(TAG, "joined %zu task(s) in array", joined);
    }
}

void task_array_handles_destroy_all(task_array_t* arr) {
    pthread_mutex_lock(&arr->lock);
    size_t count = 0;
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            task_handle_t* handle = arr->slots[i];

            // Call cleanup callback if set
            if (handle->on_cleanup != NULL) {
                handle->on_cleanup(handle);
            }

            // Clear array reference before destroy to avoid double-remove
            handle->array = NULL;
            arr->slots[i] = NULL;
            task_handle_destroy(handle);
            count++;
        }
    }
    pthread_mutex_unlock(&arr->lock);
    LOGD(TAG, "destroyed %zu task handle(s) in array", count);
}
