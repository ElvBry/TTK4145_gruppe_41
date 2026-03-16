#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/eventfd.h>


#include <rtsystem/tasks/process_pair_backup_task.h>
#include <rtsystem/rtsystem_config.h>
#include <rtsystem/core/task_helper.h>
#include <rtsystem/messages.h>



#define LOG_LEVEL LOG_LEVEL_BACKUP_TASK
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

const static char *TAG = "backup_task";

extern volatile int g_running;
extern int g_promote_fd;
extern int g_shutdown_fd;

static int backup_listen_fd = -1;
static int backup_conn_fd   = -1;

// TODO: change committed variable to be reading from and to a file instead
static process_pair_message_t committed;

process_pair_message_t process_pair_backup_get_last_committed(void) {
    return committed;
}

static int   process_pair_backup_init(task_handle_t *self, void *init_arg);
static void  process_pair_backup_cleanup(task_handle_t *self);
static void *process_pair_backup_entry(task_handle_t *self);


const task_config_t backup_task_config = {
    .priority   = PRIORITY_BACKUP,
    .on_init    = process_pair_backup_init,
    .on_cleanup = process_pair_backup_cleanup,
    .entry      = process_pair_backup_entry,
    .on_stop    = NULL,
};


static int process_pair_backup_init(task_handle_t *self, void *init_arg){
    (void)init_arg;

    errno = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE_ERRNO(TAG, "Failed to initialize socket with error: ");
        return -1;
    }
    const int enabled = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(PROCESS_PAIR_PORT),
    };
    int err = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err != 0 || listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    backup_listen_fd = fd;

    // TODO: check if log file exists, otherwise create it
    return 0;
}

static void process_pair_backup_cleanup(task_handle_t *self){
    (void)self;
    if (backup_listen_fd >= 0) {
        close(backup_listen_fd);
        backup_listen_fd = -1;
    }
    if (backup_conn_fd >= 0) {
        close(backup_conn_fd);
        backup_conn_fd = -1;
    }
}

// if timeout, break out of loop and become primary.
// Currently only takes messages in, have not implemented logic to read back that data to primary yet.
static void *process_pair_backup_entry(task_handle_t *self){

    // SO_RCVTIMEO on the listen socket causes accept() to time out just like
    // it does for recv() on a connected socket. Promote if primary is gone
    struct timeval tv = { .tv_sec  = PROCESS_PAIR_HEARTBEAT_TIMEOUT_MS / 1000,
                          .tv_usec = 0};
    setsockopt(backup_listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    LOGD(TAG, "started, waiting for primary on port %d", PROCESS_PAIR_PORT);
    backup_conn_fd = accept(backup_listen_fd, NULL, NULL);
    close(backup_listen_fd);
    backup_listen_fd = -1;

    if (backup_conn_fd < 0) {
        LOGI(TAG, "no primary appeared, promoting...");
        process_pair_backup_cleanup(self);
        task_handle_mark_done(self);
        eventfd_write(g_promote_fd, 1);
        return NULL;
    }

    LOGD(TAG, "primary connected");
   
    // Set process pair heartbeat timeout for connected socket
    setsockopt(backup_conn_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (g_running && self->state != TASK_STATE_STOPPING) {
        process_pair_message_t message;
        int received_bytes = recv(backup_conn_fd, (void *)&message, sizeof(message), MSG_WAITALL);
        if (received_bytes != sizeof(process_pair_message_t)) {
            LOGE(TAG, "primary lost, promoting...");
            break;
        }

        if (message.crc32 != process_pair_message_checksum(&message)) {
            LOGW(TAG, "checksum failed on received message, dropping...");
            send(backup_conn_fd, &committed, sizeof(committed), 0);
            continue;
        }

        if (message.type == PP_MSG_REQUEST_STATE) {
            send(backup_conn_fd, &committed, sizeof(committed), 0);
            continue;
        }

        if (message.type == PP_MSG_SHUTDOWN) {
            LOGD(TAG, "following order from primary, shutting down...");
            process_pair_backup_cleanup(self);
            task_handle_mark_done(self);
            // Wake main()'s poll loop so it sets g_running = 0 and starts shutdown.
            // This is equivalent to the user pressing Ctrl-C, but initiated by the
            // primary telling us to exit.
            eventfd_write(g_shutdown_fd, 1);
            return NULL;
        }

        committed = message;
        // TODO: write committed message to file with nplex and saferead and so on.
        send(backup_conn_fd, &committed, sizeof(committed), 0);
    }

    process_pair_backup_cleanup(self);
    task_handle_mark_done(self);
    if (g_running) {
        // Primary is gone but the system should keep running: promote to primary.
        eventfd_write(g_promote_fd, 1);
    } else {
        // g_running was cleared (SIGINT received by this process): the whole
        // system is shutting down, not just the primary. Signal main to exit.
        eventfd_write(g_shutdown_fd, 1);
    }
    return NULL;
}


