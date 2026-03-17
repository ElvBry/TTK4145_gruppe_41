#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include <rtsystem/rtsystem_config.h>
#include <rtsystem/core/task_helper.h>

#define LOG_LEVEL LOG_LEVEL_TASK_HELPER
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

static const char *TAG = "task_helper";

static int task_array_add(task_array_t *arr, task_handle_t *handle) {
    pthread_mutex_lock(&arr->lock);
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] == NULL) {
            arr->slots[i] = handle;
            handle->array  = arr;
            pthread_mutex_unlock(&arr->lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&arr->lock);
    LOGE(TAG, "task array full, cannot add task '%s'", handle->name);
    return -1;
}

static int task_array_remove(task_array_t *arr, task_handle_t *handle) {
    pthread_mutex_lock(&arr->lock);
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] == handle) {
            arr->slots[i]  = NULL;
            handle->array  = NULL;
            pthread_mutex_unlock(&arr->lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&arr->lock);
    return -1;
}

static void task_stop(task_handle_t *handle) {
    if (handle->config && handle->config->on_stop != NULL)
        handle->config->on_stop(handle);
    else
        handle->state = TASK_STATE_STOPPING;
    LOGD(TAG, "stop signal sent to task '%s'", handle->name);
}

static void task_join(task_handle_t *handle) {
    pthread_join(handle->thread, NULL);
    LOGD(TAG, "joined task '%s'", handle->name);
}

static void task_cancel(task_handle_t *handle) {
    pthread_cancel(handle->thread);
    LOGW(TAG, "cancelled task '%s'", handle->name);
}

static void task_handle_destroy(task_handle_t *handle) {
    if (handle == NULL) return;
    if (handle->array != NULL)
        task_array_remove(handle->array, handle);
    if (handle->config && handle->config->on_cleanup != NULL)
        handle->config->on_cleanup(handle);
    if (handle->done_fd != -1)
        close(handle->done_fd);
    LOGD(TAG, "destroyed task '%s'", handle->name);
    free(handle);
}

task_handle_t *task_create(task_array_t *arr, const task_config_t *config,
                           void *init_arg, const char *name) {
    if (arr == NULL || config == NULL || config->entry == NULL) {
        LOGE(TAG, "task_create: invalid arguments");
        return NULL;
    }

    task_handle_t *handle = malloc(sizeof(task_handle_t));
    if (handle == NULL) {
        LOGE(TAG, "task_create: malloc failed for task '%s'", name);
        return NULL;
    }

    *handle = (task_handle_t){
        .config         = config,
        .name           = name,
        .state          = TASK_STATE_INIT,
        .task_resources = NULL,
        .array          = NULL,
        .thread         = 0,
        .done_fd        = eventfd(0, EFD_NONBLOCK),
    };

    if (handle->done_fd == -1) {
        LOGE_ERRNO(TAG, "task_create: eventfd failed for task '%s'", name);
        free(handle);
        return NULL;
    }

    if (config->on_init != NULL && config->on_init(handle, init_arg) != 0) {
        LOGE(TAG, "task_create: on_init failed for task '%s'", name);
        close(handle->done_fd);
        free(handle);
        return NULL;
    }

    if (task_array_add(arr, handle) != 0) {
        if (config->on_cleanup != NULL) config->on_cleanup(handle);
        close(handle->done_fd);
        free(handle);
        return NULL;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (config->priority > 0) {
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        struct sched_param param = { .sched_priority = config->priority };
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    int err = pthread_create(&handle->thread, &attr,
                             (void *(*)(void *))config->entry, handle);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        LOGE(TAG, "task_create: pthread_create failed for '%s': %s", name, strerror(err));
        task_array_remove(arr, handle);
        if (config->on_cleanup != NULL) config->on_cleanup(handle);
        close(handle->done_fd);
        free(handle);
        return NULL;
    }

    LOGD(TAG, "created task '%s'", name);
    return handle;
}

void task_handle_mark_done(task_handle_t *handle) {
    handle->state = TASK_STATE_STOPPED;
    uint64_t v = 1;
    write(handle->done_fd, &v, sizeof(v));
    LOGD(TAG, "task '%s' marked done", handle->name);
}

int task_array_init(task_array_t *arr, size_t capacity) {
    arr->slots = calloc(capacity, sizeof(task_handle_t *));
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
    return 0;
}

void task_array_destroy(task_array_t *arr) {
    pthread_mutex_destroy(&arr->lock);
    free(arr->slots);
    arr->slots    = NULL;
    arr->capacity = 0;
}

void task_array_stop_all(task_array_t *arr) {
    pthread_mutex_lock(&arr->lock);
    for (size_t i = 0; i < arr->capacity; i++)
        if (arr->slots[i] != NULL)
            task_stop(arr->slots[i]);
    pthread_mutex_unlock(&arr->lock);
}

int task_array_poll_all(task_array_t *arr, int sig_fd, int timeout_ms) {
    pthread_mutex_lock(&arr->lock);

    size_t count = 0;
    for (size_t i = 0; i < arr->capacity; i++)
        if (arr->slots[i] != NULL) count++;

    if (count == 0) { pthread_mutex_unlock(&arr->lock); return 0; }

    size_t       nfds = count + (sig_fd >= 0 ? 1 : 0);
    struct pollfd *fds = malloc(nfds * sizeof(struct pollfd));
    if (fds == NULL) {
        pthread_mutex_unlock(&arr->lock);
        LOGE(TAG, "failed to allocate pollfd array");
        return -3;
    }

    size_t fi = 0;
    for (size_t i = 0; i < arr->capacity; i++)
        if (arr->slots[i] != NULL)
            fds[fi++] = (struct pollfd){ .fd = arr->slots[i]->done_fd, .events = POLLIN };
    if (sig_fd >= 0)
        fds[fi] = (struct pollfd){ .fd = sig_fd, .events = POLLIN };

    pthread_mutex_unlock(&arr->lock);

    int completed = 0;
    while (completed < (int)count) {
        int ret = poll(fds, nfds, timeout_ms);
        if (ret == -1) { free(fds); return -3; }
        if (ret ==  0) { free(fds); return -1; }
        if (sig_fd >= 0 && (fds[nfds - 1].revents & POLLIN)) { free(fds); return -2; }
        for (size_t i = 0; i < count; i++) {
            if ((fds[i].revents & POLLIN) && fds[i].fd != -1) {
                completed++;
                fds[i].fd = -1;
            }
        }
    }
    free(fds);
    return completed;
}

void task_array_cancel_all(task_array_t *arr) {
    pthread_mutex_lock(&arr->lock);
    for (size_t i = 0; i < arr->capacity; i++)
        if (arr->slots[i] != NULL)
            task_cancel(arr->slots[i]);
    pthread_mutex_unlock(&arr->lock);
    LOGW(TAG, "cancelled all tasks");
}

void task_array_join_all(task_array_t *arr) {
    pthread_mutex_lock(&arr->lock);
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            task_handle_t *h = arr->slots[i];
            struct pollfd pfd = { .fd = h->done_fd, .events = POLLIN };
            if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
                task_join(h);
            else
                LOGW(TAG, "task '%s' not finished, skipping join", h->name);
        }
    }
    pthread_mutex_unlock(&arr->lock);
}

void task_array_destroy_all(task_array_t *arr) {
    pthread_mutex_lock(&arr->lock);
    for (size_t i = 0; i < arr->capacity; i++) {
        if (arr->slots[i] != NULL) {
            task_handle_t *h = arr->slots[i];
            h->array    = NULL;
            arr->slots[i] = NULL;
            pthread_mutex_unlock(&arr->lock);
            task_handle_destroy(h);
            pthread_mutex_lock(&arr->lock);
        }
    }
    pthread_mutex_unlock(&arr->lock);
}
