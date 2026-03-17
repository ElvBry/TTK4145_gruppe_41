#include "rtsystem/messages.h"
#include <rtsystem/core/elevator_network.h>
#include "rtsystem/tasks/elevator_task.h"
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
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

// Spawns a new backup process by forking and re-executing this same binary.
// The child starts fresh and becomes the backup. Works on Desktop Ubuntu and WSL.
// Returns the PID of the spawned child so the caller can reap it with waitpid()
// when it exits. Returns -1 on failure.
static pid_t spawn_backup_process(void) {
    // /proc/self/exe always points to the currently running binary
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        LOGE_ERRNO(TAG, "readlink /proc/self/exe failed: ");
        return -1;
    }
    exe_path[len] = '\0';

    pid_t pid = fork();
    if (pid < 0) {
        LOGE_ERRNO(TAG, "fork failed: ");
        return -1;
    }
    if (pid == 0) {
        // Child process: create a new session so the backup is isolated from the
        // terminal's process group. This means Ctrl-C at the terminal does NOT
        // kill the backup directly — only the primary receives it and coordinates
        // shutdown by sending PP_MSG_SHUTDOWN. If the primary is killed with
        // kill -9, use `pkill -2 rtsystem` to stop the promoted backup gracefully.
        setsid();
        // Close all inherited file descriptors so the child starts clean.
        for (int fd = 3; fd < 1024; fd++) close(fd);
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "%d", g_elevator_id);
        char *args[] = {exe_path, "--id", id_buf, NULL};
        execv(exe_path, args);
        perror("execv failed");
        _exit(EXIT_FAILURE);
    }
    // Parent (primary) continues here.
    LOGD(TAG, "spawned new backup process (pid %d)", (int)pid);
    return pid;
}

extern volatile int g_running;

// file descriptor for socket connection with other process pair
static int primary_connection_fd = -1;

// PID of the backup process we spawned via fork+exec.
// -1 means either we haven't spawned one yet (backup was started independently)
// or we already reaped it. Used to call waitpid() when backup exits.
static pid_t backup_pid = -1;

// Last committed state received when this process was promoted from backup.
// Used to restore state after promotion. Set in primary_init when init_arg != NULL.
static process_pair_message_t initial_committed_state;


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

// Normal start:    tries to connect to an existing backup.
//   Returns  0 on success (process becomes primary).
//   Returns -1 if no backup is listening (caller should start as backup instead).
// Promoted start: init_arg points to the last committed process_pair_message_t.
//   Skips the connect attempt; primary_entry will spawn a new backup.
//   Always returns 0.
static int process_pair_primary_init(task_handle_t *self, void *init_arg) {
    (void)self;

    if (init_arg != NULL) {
        // Promoted from backup: restore last committed state, skip connect
        initial_committed_state = *(process_pair_message_t *)init_arg;
        LOGD(TAG, "promoted from backup, will spawn new backup on entry");
    } else {
        // Normal start: try to connect to an existing backup
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
            return -1;  // No backup listening, become backup instead
        }
        primary_connection_fd = fd;

        // Request backup's last committed state — read once on init, never again
        process_pair_message_t req = { .type = PP_MSG_REQUEST_STATE };
        req.crc32 = process_pair_message_checksum(&req);
        send(primary_connection_fd, &req, sizeof(req), 0);
        recv(primary_connection_fd, &initial_committed_state,
             sizeof(initial_committed_state), MSG_WAITALL);
    }

    int err = task_array_init(&application_tasks, APPLICATION_TASKS_ARRAY_CAPACITY);
    if (err != 0) {
        LOGE(TAG, "failed to initialize application tasks array");
        if (primary_connection_fd >= 0) {
            close(primary_connection_fd);
            primary_connection_fd = -1;
        }
        return -1;
    }
    LOGD(TAG, "application task array initialized, creating tasks...");
    task_handle_t *handle = task_create(&application_tasks, &elevator_task_config, &initial_committed_state, "elevator");
    if (handle == NULL) {
        LOGE(TAG, "could not initialize elevator task");
        return -1;
    }
    return 0;
}

static void process_pair_primary_cleanup(task_handle_t *self) {
    (void)self;
    task_array_stop_all(&application_tasks);
    int err = task_array_poll_all(&application_tasks, -1, APP_TASK_SHUTDOWN_TIMEOUT_MS);
    if (err < 0)
        task_array_cancel_all(&application_tasks);
    task_array_join_all(&application_tasks);
    task_array_destroy_all(&application_tasks);
    task_array_destroy(&application_tasks);
    close(primary_connection_fd);
}

static void *process_pair_primary_entry(task_handle_t *self) {
    struct timeval tv = { .tv_sec  = PROCESS_PAIR_HEARTBEAT_TIMEOUT_MS / 1000,
                          .tv_usec = 0};

    if (primary_connection_fd < 0) {
        // Promoted from backup: no connected backup yet, jump to the respawn
        // block which will spawn one and wait for it to connect.
        LOGD(TAG, "promoted from backup, spawning new backup...");
        goto respawn;
    }
    setsockopt(primary_connection_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    LOGD(TAG, "connected to backup");
        
    while (g_running && self->state != TASK_STATE_STOPPING) {
        pthread_mutex_lock(&my_elevator.lock);
        process_pair_message_t message = {
            .type              = PP_MSG_HEARTBEAT,
            .my_elevator_state = my_elevator.elevator_state,
            .worldview         = my_elevator.worldview,
        };
        pthread_mutex_unlock(&my_elevator.lock);
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

        if (primary_connection_fd >= 0) {
            close(primary_connection_fd);
            primary_connection_fd = -1;
        }

        // Reap the dead backup so it does not become a zombie.
        // WNOHANG returns immediately: if backup already exited it is reaped now;
        // if it somehow hasn't exited yet we move on and it will be reaped
        // when SIG_IGN auto-reaps it (set in main()).
        if (backup_pid > 0) {
            waitpid(backup_pid, NULL, WNOHANG);
            backup_pid = -1;
        }

        LOGD(TAG, "spawning new backup...");
        backup_pid = spawn_backup_process();

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
                setsockopt(primary_connection_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                LOGD(TAG, "reconnected to new backup");
                break;
            }
            close(fd);
        }
    }
    LOGD(TAG, "received shutdown request...");
    if (primary_connection_fd >= 0) {
        // Connected: ask backup to shut down gracefully via the heartbeat channel.
        // send() returns -1/EPIPE (not SIGPIPE) because we set SIG_IGN in main().
        LOGD(TAG, "shutting down, sending shutdown order to backup");
        process_pair_message_t shutdown_msg = { .type = PP_MSG_SHUTDOWN };
        shutdown_msg.crc32 = process_pair_message_checksum(&shutdown_msg);
        send(primary_connection_fd, &shutdown_msg, sizeof(shutdown_msg), 0);
    } else if (backup_pid > 0) {
        // No connection yet: primary was shut down while still waiting for the
        // newly spawned backup to start listening. We never established a channel
        // to send PP_MSG_SHUTDOWN, so terminate the backup process directly.
        LOGD(TAG, "shutting down before backup connected, terminating backup (pid %d)", (int)backup_pid);
        kill(backup_pid, SIGTERM);
    }

    // Wait for the backup process to exit (cleanly after PP_MSG_SHUTDOWN, or
    // immediately after SIGTERM). If backup was already dead (kill -9) it is
    // already a zombie and waitpid returns immediately.
    // If backup_pid is -1 (backup was started independently, not by us), skip.
    if (backup_pid > 0) {
        waitpid(backup_pid, NULL, 0);
        backup_pid = -1;
    }

    LOGD(TAG, "backup exited, primary done");
    task_handle_mark_done(self);
    return NULL;
}