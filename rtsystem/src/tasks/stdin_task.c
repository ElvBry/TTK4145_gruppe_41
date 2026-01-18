#include <stdlib.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/tasks/stdin_task.h>
#include <rtsystem/async_log_helper.h>

#define STDIN_POLL_TIMEOUT_MS 100

static const char *TAG = "stdin_task";

extern volatile int g_running;

// Global task handle
task_handle_t g_stdin_handle;

// Input buffer
static char *line = NULL;

static void *stdin_task(void *arg) {
    stdin_arg_t *stdin_arg = (stdin_arg_t *)arg;
    size_t line_buf_size = stdin_arg->buf_size;
    struct pollfd fds = {
        .fd     = STDIN_FILENO,
        .events = POLLIN,
    };

    g_stdin_handle.state = TASK_STATE_RUNNING;
    LOGD(TAG, "successfully initialized. ready for input...");

    while (g_running && g_stdin_handle.state == TASK_STATE_RUNNING) {
        int err = poll(&fds, 1, STDIN_POLL_TIMEOUT_MS);

        if (err == -1) {
            LOGW_ERRNO(TAG, "could not poll STDIN: ");
            errno = 0;
            continue;
        }

        if (!(fds.revents & POLLIN)) {
            continue;
        }

        ssize_t bytes_read = read(STDIN_FILENO, line, line_buf_size - 1);

        if (bytes_read == -1) {
            LOGW_ERRNO(TAG, "failed to read input: ");
            continue;
        }

        if (bytes_read == line_buf_size - 1) {
            LOGW(TAG, "Message longer than or equal to: %zu bytes, splitting up into multiple parts", line_buf_size - 1);
        }

        line[bytes_read] = '\0';

        // Strip trailing newline
        if (bytes_read > 0 && line[bytes_read - 1] == '\n') {
            line[bytes_read - 1] = '\0';
            bytes_read--;
        }

        // Ignore empty lines
        if (bytes_read == 0) {
            continue;
        }

        LOGD(TAG, "input received: %s", line);
    }

    LOGD(TAG, "received shutdown signal, exiting gracefully");
    free(line);
    line = NULL;
    LOGD(TAG, "freed buffer, exiting...");

    task_handle_mark_done(&g_stdin_handle);
    return NULL;
}

int stdin_task_init(const size_t buf_size, const int priority) {
    if (task_handle_init(&g_stdin_handle, "stdin_task") != 0) {
        LOGE(TAG, "Failed to initialize task handle");
        return -1;
    }

    line = malloc(buf_size);
    if (!line) {
        LOGE(TAG, "Malloc failed to initialize buffer of size %zu", buf_size);
        task_handle_destroy(&g_stdin_handle);
        return -1;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (priority > 0) {
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        struct sched_param param = { .sched_priority = priority };
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    stdin_arg_t arg = {.buf_size = buf_size};
    int err = pthread_create(&g_stdin_handle.thread, &attr, stdin_task, &arg);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        LOGE_ERRNO(TAG, "Could not perform pthread_create: ");
        free(line);
        line = NULL;
        task_handle_destroy(&g_stdin_handle);
        return -1;
    }

    return 0;
}

void stdin_task_stop(void) {
    g_stdin_handle.state = TASK_STATE_STOPPING;
}

void stdin_task_join(void) {
    pthread_join(g_stdin_handle.thread, NULL);
}

void stdin_task_cancel(void) {
    pthread_cancel(g_stdin_handle.thread);
}

void stdin_task_destroy(void) {
    task_handle_destroy(&g_stdin_handle);
}
