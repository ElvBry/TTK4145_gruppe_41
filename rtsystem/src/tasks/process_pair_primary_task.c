#include "rtsystem/messages.h"
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

#include <rtsystem/rtsystem_config.h>
#include <rtsystem/core/task_helper.h>
#include <rtsystem/tasks/process_pair_primary_task.h>

#define LOG_LEVEL LOG_LEVEL_PRIMARY_TASK
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

const static char *TAG = "primary_task";

extern volatile int g_running;

// file descriptor for socket connection with other process pair
static int primary_connection_fd = -1;

char *reason = "temporary reason for shutdown, add logic later";

static int   process_pair_primary_init(task_handle_t *self, void *init_arg);
static void  process_pair_primary_cleanup(task_handle_t *self);
static void *process_pair_primary_entry(task_handle_t *self);

static task_array_t application_tasks;

const task_config_t primary_task_config = {
    .priority   = PRIORITY_PRIMARY,
    .on_init    = process_pair_primary_init,
    .on_cleanup = process_pair_primary_cleanup,
    .entry      = process_pair_primary_entry,
    .on_stop    = NULL,
};

// Tries to connect to existing backup
// Returns 0 if a backup is listening (process becomes primary)
// Returns -1 if no backup is listening (process becomes backup)
static int process_pair_primary_init(task_handle_t *self, void *init_arg) {
    (void)init_arg;
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port        = htons(PROCESS_PAIR_PORT),
    };

    errno = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE_ERRNO(TAG, "failed to initialize socket with error: ");
        exit(EXIT_FAILURE);
    }

    int err = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err != 0) {
        close(fd);
        // backup is not on the other end yet, become backup instead
        return -1;
    }
    primary_connection_fd = fd;

    err = task_array_init(&application_tasks, APPLICATION_TASKS_ARRAY_CAPACITY);
    if (err != 0) {
        LOGE(TAG, "failed to initialize system tasks array");
        return -1;
    }
    LOGD(TAG, "application task array initialized, creating tasks...");
    // Create tasks needed for program
    
    return 0;
}

static void process_pair_primary_cleanup(task_handle_t *self) {
    (void)self;
    task_array_stop_all(&application_tasks);
    int err = task_array_poll_all(&application_tasks, -1, APP_TASK_SHUTDOWN_TIMEOUT_MS);
    if (err != 0) 
        task_array_cancel_all(&application_tasks);
    task_array_join_all(&application_tasks);
    task_array_destroy_all(&application_tasks);
    task_array_destroy(&application_tasks);
    close(primary_connection_fd);
}

static void *process_pair_primary_entry(task_handle_t *self) {
    struct timeval tv = { .tv_sec  = PROCESS_PAIR_HEARTBEAT_TIMEOUT_MS / 1000,
                          .tv_usec = 0};
    setsockopt(primary_connection_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    LOGD(TAG, "connected to backup");
        
    while (g_running && self->state != TASK_STATE_STOPPING) {
        // TODO: get current state from value found by elevator_task
        elevator_state_t state = {
            .payload_placeholder = 0,
        };
        // TODO: get current worldview from value found by elevator_task
        worldview_t worldview = {
            .worldview_counter = 0,
            .hall_requests = {{0}},
        };
        process_pair_message_t message = {
            .my_elevator_state = state,
            .worldview         = worldview,
        };
        message.crc32 = process_pair_message_checksum(&message);

        ssize_t sent_bytes = send(primary_connection_fd, &message, sizeof(message), 0);
        if (sent_bytes != (ssize_t)sizeof(message)) {
            LOGE(TAG, "send failed, backup gone");
            goto respawn;
        }

        process_pair_message_t echo;
        ssize_t n = recv(primary_connection_fd, &echo, sizeof(echo), MSG_WAITALL);
        if (n != sizeof(echo)) {
            LOGE(TAG, "no echo, backup gone or timed out");
            goto respawn;
        }

        if (echo.crc32 != process_pair_message_checksum(&echo)) {
            // NOTE: this might be a sign of a corrupt backup, not handled in any way
            LOGE(TAG, "checksum fail on echo, dropping...");
            continue;
        }
        // TODO: use echo (confirmed backup state) to update worldview

        usleep(PROCESS_PAIR_HEARTBEAT_MS * 1000);
        continue;

    respawn:
        if (!g_running || self->state == TASK_STATE_STOPPING)
            break;

        close(primary_connection_fd);
        primary_connection_fd = -1;

        LOGD(TAG, "spawning new backup...");
        system(SPAWN_CMD);

        struct sockaddr_in addr = {
            .sin_family      = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
            .sin_port        = htons(PROCESS_PAIR_PORT),
        };
        while (g_running && self->state != TASK_STATE_STOPPING) {
            usleep(PROCESS_PAIR_HEARTBEAT_MS * 1000);
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                primary_connection_fd = fd;
                LOGD(TAG, "reconnected to new backup");
                break;
            }
            close(fd);
        }
    }
    LOGD(TAG, "received shutdown signal with reason: %s", reason);
    process_pair_primary_cleanup(self);
    LOGD(TAG, "exiting...");
    task_handle_mark_done(self);
    return NULL;
}