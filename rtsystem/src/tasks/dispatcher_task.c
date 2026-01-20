#include "rtsystem/core/fifo_queue.h"
#include <poll.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/core/task_helper.h>
#include <rtsystem/tasks/dispatcher_task.h>
#include <rtsystem/async_log_helper.h>
#include <rtsystem/core/cmd_parser.h>

// Dispatcher that takes in commands and handles input. Takes in commands from terminal and main or other computers through network

#define DISPATCHER_POLL_TIMEOUT_MS 100

static const char *TAG = "disp_task";

extern volatile int g_running;

static task_handle_t handle;

// Task array (capacity 1)
task_array_t g_dispatcher_task;

static fifo_queue_t command_queue;
static bool command_queue_initialized = false;

static void dispatcher_cleanup(task_handle_t* self) {
    (void)self;
    if (command_queue_initialized) {
        fifo_queue_destroy(&command_queue);
        command_queue_initialized = false;
        LOGD(TAG, "destroyed command queue");
        return;
    }
    LOGD(TAG, "tried to cleanup, but command queue was already destroyed");
}

static void *dispatcher_task(void *arg) {
    (void)arg;
    struct pollfd fds = {
        .fd     = command_queue.event_fd,
        .events = POLLIN,
    };

    char *message = "";

    handle.state = TASK_STATE_RUNNING;
    LOGD(TAG, "successfully initialized. Dispatching commands...");

    while (g_running && handle.state == TASK_STATE_RUNNING) {
        int err = poll(&fds, 1, DISPATCHER_POLL_TIMEOUT_MS);

        if (err == 0) {
            // Timout, normal and wanted for checking g_running and if state or for checking heartbeat later on with watchdog
            continue;
        }

        if (err == -1) {
            LOGW_ERRNO(TAG, "could not poll command queue");
            errno = 0;
            continue;
        }        

        if (!(fds.revents & POLLIN)) {
            LOGW(TAG, "should not happen, check cause");
        }
        cmd_t command;
        err = fifo_queue_receive(&command_queue, &command);
        if (err == -1) {
            LOGW(TAG, "could not receive command from queue");
            continue;
        }

        switch (command.cmd_type) {
            case UDP:
                parse_UDP(command);
                break;
            case TCP:
                parse_TCP(command);
                break;
            case ECHO:
                parse_ECHO(command, &message);
                LOGI(TAG, "%s", message);
                break;
            case HELP:
                parse_HELP(command, &message);
                LOGI(TAG, "%s", message);
                break;
            case NIL:
                LOGW(TAG, "received NIL, not a valid command (type 'help' for help)");
                parse_NIL(command);
                break;
            default:
                LOGE(TAG, "in default branch, should not happen");
                break;
        }
        message = "";
        
        cmd_free(&command);
    }
    LOGD(TAG, "exiting...");
    task_handle_mark_done(&handle);
    return NULL;
}


int dispatcher_add_to_queue(cmd_t command) {
    if (!command_queue_initialized) {
        LOGE(TAG, "tried to add command to queue before it was initialized");
        return -1;
    }
    int err = fifo_queue_send(&command_queue, (const void *) &command);
    if (err != 0) {
        LOGE(TAG, "log queue full");
        return -1;
    }
    return 0;
}

int dispatcher_task_init(const size_t cmd_queue_size, const int priority) {
    // TODO: Task initialization is very similar for stdin_task and dispatcer_task, will likely be same when adding more
    // Could refactor with new function in task_helper
    int err = task_array_init(&g_dispatcher_task, 1);
    if (err != 0) {
        LOGE(TAG, "failed to initialize task array");
        return -1;
    }

    err = task_handle_init(&handle, TAG);
    if (err != 0) {
        LOGE(TAG, "failed to initialize task handle");
        task_array_destroy(&g_dispatcher_task);
        return -1;
    }

    handle.on_cleanup = dispatcher_cleanup;

    // Add to array (will fail if called twice due to capacity 1)
    err = task_array_add(&g_dispatcher_task, &handle);
    if (err != 0) {
        LOGE(TAG, "task already exists");
        task_handle_destroy(&handle);
        task_array_destroy(&g_dispatcher_task);
        return -1;
    }

    // Could make generic initialize_resources function in task_handler and reuse for dispatcher, stdin and new tasks
    if (!command_queue_initialized) {
        err = fifo_queue_init(&command_queue, sizeof(cmd_t), cmd_queue_size);
        if (err != 0) {
            LOGE(TAG, "failed to allocate command queue of capacity %zu", cmd_queue_size);
            return -1;
        }
        command_queue_initialized = true;
    }
   
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (priority > 0) {
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        struct sched_param param = { .sched_priority = priority };
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    static size_t arg;
    arg = cmd_queue_size;
    err = pthread_create(&handle.thread, &attr, dispatcher_task, &arg);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        LOGE_ERRNO(TAG, "pthread_create failed: ");
        errno = 0;
        fifo_queue_destroy(&command_queue);
        command_queue_initialized = false;
        return -1;
    }
    return 0;
}