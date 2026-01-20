#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/tasks/stdin_task.h>
#include <rtsystem/async_log_helper.h>
#include <rtsystem/core/cmd_parser.h>
#include <rtsystem/tasks/dispatcher_task.h>

#define STDIN_POLL_TIMEOUT_MS 10

static const char *TAG = "stdin_task";

extern volatile int g_running;

typedef struct {
    char *line;
    size_t buf_size;
} stdin_data_t;

static int stdin_init(task_handle_t *self, void *init_arg);
static void stdin_cleanup(task_handle_t *self);
static void *stdin_entry(task_handle_t *self);

const task_config_t stdin_task_config = {
    .name       = "stdin_task",
    .priority   = DEFAULT_STDIN_TASK_PRIORITY,
    .entry      = stdin_entry,
    .on_init    = stdin_init,
    .on_stop    = NULL,
    .on_cleanup = stdin_cleanup,
};


static int stdin_init(task_handle_t *self, void *init_arg) {
    size_t buf_size = *(size_t *)init_arg;

    stdin_data_t *data = malloc(sizeof(stdin_data_t));
    if (data == NULL) {
        LOGE(TAG, "malloc failed for stdin_data_t");
        return -1;
    }

    data->buf_size = buf_size;
    data->line = malloc(buf_size);
    if (data->line == NULL) {
        LOGE(TAG, "malloc failed for input buffer of size %zu", buf_size);
        free(data);
        return -1;
    }

    self->task_resources = data;
    LOGD(TAG, "allocated input buffer of size %zu", buf_size);
    return 0;
}

static void stdin_cleanup(task_handle_t *self) {
    stdin_data_t *data = self->task_resources;
    if (data != NULL) {
        free(data->line);
        free(data);
        self->task_resources = NULL;
        LOGD(TAG, "freed input buffer");
    }
}


static void *stdin_entry(task_handle_t *self) {
    stdin_data_t *data = self->task_resources;

    struct pollfd fds = {
        .fd     = STDIN_FILENO,
        .events = POLLIN,
    };

    self->state = TASK_STATE_RUNNING;
    LOGD(TAG, "ready for input...");

    while (g_running && self->state != TASK_STATE_STOPPING) {
        int err = poll(&fds, 1, STDIN_POLL_TIMEOUT_MS);

        if (err == -1) {
            LOGW_ERRNO(TAG, "could not poll STDIN: ");
            continue;
        }

        if (err == 0 || !(fds.revents & POLLIN)) {
            continue;
        }

        ssize_t bytes_read = read(STDIN_FILENO, data->line, data->buf_size - 1);

        if (bytes_read == -1) {
            LOGW_ERRNO(TAG, "failed to read input: ");
            continue;
        }

        if (bytes_read == 0) {
            continue;
        }

        data->line[bytes_read] = '\0';

        // Check if input overflowed buffer
        bool overflowed = (size_t)bytes_read == data->buf_size - 1 &&
                          data->line[bytes_read - 1] != '\n';
        if (overflowed) {
            LOGW(TAG, "input >= %zu bytes, flushing rest of data", data->buf_size - 1);

            char flush_buf[64];
            while (1) {
                ssize_t n = read(STDIN_FILENO, flush_buf, sizeof(flush_buf));
                if (n <= 0) break;
                if (memchr(flush_buf, '\n', n)) break;
            }
        }

        // Strip trailing newline
        if (bytes_read > 0 && data->line[bytes_read - 1] == '\n') {
            data->line[bytes_read - 1] = '\0';
            bytes_read--;
        }

        // Ignore empty lines
        if (bytes_read == 0) {
            continue;
        }

        LOGD(TAG, "received: %s", data->line);

        // Tokenize into temporary stack array
        char *argv_tmp[MAX_ARGS];
        int argc = tokenize(data->line, argv_tmp);

        if (argc == 0) {
            continue;
        }

        // Deep copy for async processing (dispatcher will free)
        char **argv = malloc(argc * sizeof(char *));
        if (argv == NULL) {
            LOGE(TAG, "malloc failed for argv");
            continue;
        }

        bool alloc_failed = false;
        for (int i = 0; i < argc; i++) {
            argv[i] = strdup(argv_tmp[i]);
            if (argv[i] == NULL) {
                LOGE(TAG, "strdup failed");
                for (int j = 0; j < i; j++) {
                    free(argv[j]);
                }
                free(argv);
                alloc_failed = true;
                break;
            }
        }
        if (alloc_failed) {
            continue;
        }

        cmd_t command = {
            .argc = argc,
            .argv = argv,
        };

        err = set_cmd_type(&command);
        if (err == -1) {
            LOGE(TAG, "could not set command type");
            cmd_free(&command);
            continue;
        }

        dispatcher_add_to_queue(command);
    }

    LOGD(TAG, "exiting...");
    task_handle_mark_done(self);
    return NULL;
}
