#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/core/task_helper.h>
#include <rtsystem/async_log_helper.h>

static const char *TAG = "task_helper";

task_handle_t* task_create(task_array_t* arr, const task_config_t* config, void* init_arg) {
    if (arr == NULL || config == NULL || config->entry == NULL) {
        LOGE(TAG, "task_create: invalid arguments");
        return NULL;
    }

    task_handle_t* handle = malloc(sizeof(task_handle_t));
    if (handle == NULL) {
        LOGE(TAG, "task_create: malloc failed for task '%s'", config->name);
        return NULL;
    }

    handle->config = config;
    handle->state = TASK_STATE_INIT;
    handle->task_resources = NULL;
    handle->array = NULL;
    handle->thread = 0;

    handle->done_fd = eventfd(0, EFD_NONBLOCK);
    if (handle->done_fd == -1) {
        LOGE_ERRNO(TAG, "task_create: eventfd failed for task '%s'", config->name);
        free(handle);
        return NULL;
    }

    // Call on_init if provided
    if (config->on_init != NULL) {
        int err = config->on_init(handle, init_arg);
        if (err != 0) {
            LOGE(TAG, "task_create: on_init failed for task '%s'", config->name);
            close(handle->done_fd);
            free(handle);
            return NULL;
        }
    }

    // Add to array
    int err = task_array_add(arr, handle);
    if (err != 0) {
        LOGE(TAG, "task_create: failed to add task '%s' to array", config->name);
        if (config->on_cleanup != NULL) {
            config->on_cleanup(handle);
        }
        close(handle->done_fd);
        free(handle);
        return NULL;
    }

    // Setup thread attributes with priority
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (config->priority > 0) {
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        struct sched_param param = { .sched_priority = config->priority };
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    // Create thread - entry receives handle as argument
    err = pthread_create(&handle->thread, &attr, (void*(*)(void*))config->entry, handle);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        LOGE(TAG, "task_create: pthread_create failed for task '%s': %s",
             config->name, strerror(err));
        task_array_remove(arr, handle);
        if (config->on_cleanup != NULL) {
            config->on_cleanup(handle);
        }
        close(handle->done_fd);
        free(handle);
        return NULL;
    }

    LOGD(TAG, "created task '%s'", config->name);
    return handle;
}

void task_handle_mark_done(task_handle_t* handle) {
    handle->state = TASK_STATE_STOPPED;
    uint64_t done = 1;
    write(handle->done_fd, &done, sizeof(done));
    LOGD(TAG, "task '%s' marked done", handle->config->name);
}

void task_handle_destroy(task_handle_t* handle) {
    if (handle == NULL) return;

    const char* name = handle->config ? handle->config->name : "unknown";

    // Remove from array if still registered
    if (handle->array != NULL) {
        int err = task_array_remove(handle->array, handle);
        if (err != 0) {
            LOGW(TAG, "could not find handle of name: '%s' to remove", handle->config->name);
        }
    }

    // Call cleanup callback
    if (handle->config && handle->config->on_cleanup != NULL) {
        handle->config->on_cleanup(handle);
    }

    // Close done_fd
    if (handle->done_fd != -1) {
        close(handle->done_fd);
        handle->done_fd = -1;
    }

    handle->state = TASK_STATE_STOPPED;
    LOGD(TAG, "destroyed task '%s'", name);

    free(handle);
}





void task_stop(task_handle_t* handle) {
    if (handle->config && handle->config->on_stop != NULL) {
        handle->config->on_stop(handle);
    } else {
        handle->state = TASK_STATE_STOPPING;
    }
    LOGD(TAG, "stop signal sent to task '%s'", handle->config->name);
}

void task_join(task_handle_t* handle) {
    pthread_join(handle->thread, NULL);
    LOGD(TAG, "joined task '%s'", handle->config->name);
}

void task_cancel(task_handle_t* handle) {
    pthread_cancel(handle->thread);
    LOGW(TAG, "cancelled task '%s'", handle->config->name);
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
            LOGD(TAG, "added task '%s' to array at slot %zu", handle->config->name, i);
            return 0;
        }
    }

    pthread_mutex_unlock(&arr->lock);
    LOGE(TAG, "task array full, cannot add task '%s'", handle->config->name);
    return -1;
}

int task_array_remove(task_array_t* arr, task_handle_t* handle) {
    pthread_mutex_lock(&arr->lock);

    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] == handle) {
            arr->slots[i] = NULL;
            handle->array = NULL;
            pthread_mutex_unlock(&arr->lock);
            LOGD(TAG, "removed task '%s' from array slot %zu", handle->config->name, i);
            return 0;
        }
    }

    pthread_mutex_unlock(&arr->lock);
    LOGW(TAG, "task '%s' not found in array", handle->config->name);
    return -1;
}

size_t task_array_count(task_array_t* arr) {
    pthread_mutex_lock(&arr->lock);
    size_t count = 0;
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            count++;
        }
    }
    pthread_mutex_unlock(&arr->lock);
    return count;
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
    LOGD(TAG, "sent stop signal to %zu task(s)", count);
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
    struct pollfd *fds = malloc(nfds * sizeof(struct pollfd));
    if (fds == NULL) {
        pthread_mutex_unlock(&arr->lock);
        LOGE(TAG, "failed to allocate pollfd array");
        return -3;
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

    // Poll until all tasks complete, timeout, or signal
    int completed = 0;

    while (completed < (int)count) {
        int ret = poll(fds, nfds, timeout_ms);

        if (ret == -1) {
            LOGW_ERRNO(TAG, "poll failed in task_array_poll_all: ");
            free(fds);
            return -3;
        }

        if (ret == 0) {
            // Timeout - no tasks completed within timeout_ms
            free(fds);
            return -1;
        }

        // Check if signal fd triggered (force shutdown)
        if (sig_fd >= 0 && (fds[nfds - 1].revents & POLLIN)) {
            free(fds);
            return -2;
        }

        // Count newly completed tasks
        for (size_t i = 0; i < count; i++) {
            if (fds[i].revents & POLLIN && fds[i].fd != -1) {
                completed++;
                fds[i].fd = -1;  // Mark done so poll ignores it
            }
        }
    }

    free(fds);
    return completed;
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
    LOGW(TAG, "cancelled %zu task(s)", count);
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
                LOGW(TAG, "task '%s' not finished, skipping join", handle->config->name);
                skipped++;
            }
        }
    }
    pthread_mutex_unlock(&arr->lock);

    if (skipped > 0) {
        LOGW(TAG, "joined %zu task(s), skipped %zu unfinished task(s)", joined, skipped);
    } else {
        LOGD(TAG, "joined %zu task(s)", joined);
    }
}

void task_array_destroy_all(task_array_t* arr) {
    pthread_mutex_lock(&arr->lock);
    size_t count = 0;
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            task_handle_t* handle = arr->slots[i];

            // Clear array reference before destroy to avoid double-remove
            handle->array = NULL;
            arr->slots[i] = NULL;

            // Destroy handle (calls on_cleanup, frees memory)
            task_handle_destroy(handle);
            count++;
        }
    }
    pthread_mutex_unlock(&arr->lock);
    LOGD(TAG, "destroyed %zu task(s)", count);
}




int task_array_reap_finished(task_array_t* arr) {
    pthread_mutex_lock(&arr->lock);
    int reaped = 0;

    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            task_handle_t* handle = arr->slots[i];

            // Check if task has finished (done_fd readable)
            struct pollfd pfd = { .fd = handle->done_fd, .events = POLLIN };
            if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
                LOGD(TAG, "reaping finished task '%s'", handle->config->name);

                // Clear array reference before destroy
                handle->array = NULL;
                arr->slots[i] = NULL;

                // Join and destroy
                pthread_mutex_unlock(&arr->lock);
                task_join(handle);
                task_handle_destroy(handle);
                pthread_mutex_lock(&arr->lock);

                reaped++;
            }
        }
    }

    pthread_mutex_unlock(&arr->lock);

    if (reaped > 0) {
        LOGD(TAG, "reaped %d finished task(s)", reaped);
    }
    return reaped;
}
