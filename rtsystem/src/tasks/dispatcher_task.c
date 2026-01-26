#include <poll.h>
#include <stdlib.h>
#include <stdbool.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/core/task_helper.h>
#include <rtsystem/core/fifo_queue.h>
#include <rtsystem/tasks/dispatcher_task.h>
#include <rtsystem/async_log_helper.h>
#include <rtsystem/core/cmd_parser.h>

#define DISPATCHER_POLL_TIMEOUT_MS 10

static const char *TAG = "disp_task";

extern volatile int g_running;

static fifo_queue_t g_command_queue;
static bool g_command_queue_initialized = false;

static int   dispatcher_init(task_handle_t *self, void *init_arg);
static void  dispatcher_cleanup(task_handle_t *self);
static void *dispatcher_entry(task_handle_t *self);

const task_config_t dispatcher_task_config = {
    .priority   = DEFAULT_DISPATCHER_TASK_PRIORITY,
    .entry      = dispatcher_entry,
    .on_init    = dispatcher_init,
    .on_stop    = NULL,
    .on_cleanup = dispatcher_cleanup,
};

static int dispatcher_init(task_handle_t *self, void *init_arg) {
    size_t queue_size = *(size_t *)init_arg;

    if (g_command_queue_initialized) {
        LOGW(TAG, "command queue already initialized");
        return 0;
    }

    int err = fifo_queue_init(&g_command_queue, sizeof(cmd_t), queue_size);
    if (err != 0) {
        LOGE(TAG, "failed to initialize command queue of capacity %zu", queue_size);
        return -1;
    }

    g_command_queue_initialized = true;
    LOGD(TAG, "initialized command queue with capacity %zu", queue_size);
    return 0;
}

static void dispatcher_cleanup(task_handle_t *self) {
    (void)self;
    if (g_command_queue_initialized) {
        fifo_queue_destroy(&g_command_queue);
        g_command_queue_initialized = false;
        LOGD(TAG, "destroyed command queue");
    }
}

static void *dispatcher_entry(task_handle_t *self) {
    struct pollfd fds = {
        .fd     = g_command_queue.event_fd,
        .events = POLLIN,
    };

    char *message = "";

    self->state = TASK_STATE_RUNNING;
    LOGD(TAG, "ready to dispatch commands...");

    while (g_running && self->state != TASK_STATE_STOPPING) {
        int err = poll(&fds, 1, DISPATCHER_POLL_TIMEOUT_MS);

        if (err == 0) {
            // Timeout

            continue;
        }

        if (err == -1) {
            LOGW_ERRNO(TAG, "could not poll command queue: ");
            continue;
        }

        if (!(fds.revents & POLLIN)) {
            LOGW(TAG, "poll returned but no POLLIN, should not happen");
            continue;
        }

        cmd_t command;
        err = fifo_queue_receive(&g_command_queue, &command);
        if (err == -1) {
            LOGW(TAG, "could not receive command from queue");
            continue;
        }

        switch (command.cmd_type) {
            case SOCKET:
                parse_socket(command);
                break;
            case ECHO:
                parse_echo(command, &message);
                LOGI(TAG, "%s", message);
                break;
            case HELP:
                parse_help(command, &message);
                LOGI(TAG, "%s", message);
                break;
            case NIL:
                LOGW(TAG, "received NIL, not a valid command (type 'help' for help)");
                parse_NIL(command);
                break;
            default:
                LOGE(TAG, "unknown command type %d", command.cmd_type);
                break;
        }

        message = "";
        cmd_free(&command);
    }
    dispatcher_cleanup(self);
    LOGD(TAG, "exiting...");
    task_handle_mark_done(self);
    return NULL;
}

int dispatcher_add_to_queue(cmd_t command) {
    if (!g_command_queue_initialized) {
        LOGE(TAG, "command queue not initialized");
        return -1;
    }

    int err = fifo_queue_send(&g_command_queue, (const void *)&command);
    if (err != 0) {
        LOGE(TAG, "command queue full");
        return -1;
    }

    return 0;
}