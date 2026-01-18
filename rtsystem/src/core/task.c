#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <stdlib.h>

#include <rtsystem/core/task.h>

int task_handle_init(task_handle_t* handle, const char* name) {
    handle->thread = 0;
    handle->state = TASK_STATE_INIT;
    handle->name = name;

    handle->done_fd = eventfd(0, EFD_NONBLOCK);
    if (handle->done_fd == -1) {
        return -1;
    }

    return 0;
}

void task_handle_destroy(task_handle_t* handle) {
    if (handle->done_fd != -1) {
        close(handle->done_fd);
        handle->done_fd = -1;
    }
    handle->state = TASK_STATE_STOPPED;
}

void task_handle_mark_done(task_handle_t* handle) {
    handle->state = TASK_STATE_STOPPED;
    uint64_t done = 1;
    write(handle->done_fd, &done, sizeof(done));
}

int task_handle_wait(task_handle_t* handle, int timeout_ms) {
    struct pollfd pfd = {
        .fd = handle->done_fd,
        .events = POLLIN
    };

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        return 0;
    }
    return -1;
}

int task_handle_is_done(task_handle_t* handle) {
    return handle->state == TASK_STATE_STOPPED;
}

int task_handle_wait_multiple(task_handle_t** handles, size_t count, int timeout_ms) {
    struct pollfd* fds = malloc(count * sizeof(struct pollfd));
    if (!fds) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        fds[i].fd = handles[i]->done_fd;
        fds[i].events = POLLIN;
    }

    int ret = poll(fds, count, timeout_ms);
    if (ret < 0) {
        free(fds);
        return -1;
    }

    int completed = 0;
    for (size_t i = 0; i < count; i++) {
        if (fds[i].revents & POLLIN) {
            completed++;
        }
    }

    free(fds);
    return completed;
}
