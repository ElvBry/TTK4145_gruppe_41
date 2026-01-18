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

// Task array (capacity 1)
task_array_t g_stdin_tasks;

static task_handle_t handle;

// Input buffer
static char *line = NULL;

static void stdin_cleanup(task_handle_t* self) {
    (void)self;
    if (line != NULL) {
        free(line);
        line = NULL;
        LOGD(TAG, "freed input buffer");
    }
}

static void *stdin_task(void *arg) {
    size_t line_buf_size = *(size_t*)arg;
    struct pollfd fds = {
        .fd     = STDIN_FILENO,
        .events = POLLIN,
    };

    handle.state = TASK_STATE_RUNNING;
    LOGD(TAG, "ready for input...");

    while (g_running && handle.state == TASK_STATE_RUNNING) {
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
            errno = 0;
            continue;
        }

        if ((size_t)bytes_read == line_buf_size - 1) {
            LOGW(TAG, "input >= %zu bytes, message might be split", line_buf_size - 1);
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

    LOGD(TAG, "exiting...");
    task_handle_mark_done(&handle);
    return NULL;
}

int stdin_task_init(const size_t buf_size, const int priority) {
    // Initialize task array (capacity 1 - only one stdin task)
    if (task_array_init(&g_stdin_tasks, 1) != 0) {
        LOGE(TAG, "failed to initialize task array");
        return -1;
    }

    if (task_handle_init(&handle, "stdin") != 0) {
        LOGE(TAG, "failed to initialize task handle");
        task_array_destroy(&g_stdin_tasks);
        return -1;
    }

    // Set cleanup callback
    handle.on_cleanup = stdin_cleanup;

    // Add to array (will fail if called twice due to capacity 1)
    if (task_array_add(&g_stdin_tasks, &handle) != 0) {
        LOGE(TAG, "stdin task already exists");
        task_handle_destroy(&handle);
        task_array_destroy(&g_stdin_tasks);
        return -1;
    }

    line = malloc(buf_size);
    if (!line) {
        LOGE(TAG, "malloc failed for buffer of size %zu", buf_size);
        task_handle_destroy(&handle);
        task_array_destroy(&g_stdin_tasks);
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

    static size_t buf_size_arg;
    buf_size_arg = buf_size;
    int err = pthread_create(&handle.thread, &attr, stdin_task, &buf_size_arg);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        LOGE_ERRNO(TAG, "pthread_create failed: ");
        errno = 0;
        free(line);
        line = NULL;
        task_handle_destroy(&handle);
        task_array_destroy(&g_stdin_tasks);
        return -1;
    }

    return 0;
}
