#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


#include <rtsystem/tasks/process_pair_backup_task.h>
#include <rtsystem/rtsystem_config.h>
#include <rtsystem/core/task_helper.h>
#include <rtsystem/core/elevator_network.h>
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

// GN
#define NPLEX 2

// Only data stored on disk: crc32 (covers seq+data), seq, and the message.
// crc32 is first so that {seq, data} form a contiguous region for checksum.
typedef struct {
    uint32_t               crc32;
    uint32_t               seq;
    process_pair_message_t data;
} store_block_t;

static const char *g_store_paths[NPLEX] = {
    "/tmp/rtsystem_pp_backup_0.dat",
    "/tmp/rtsystem_pp_backup_1.dat",
};
static uint32_t g_seq = 0;

static uint32_t store_block_checksum(const store_block_t *b) {
    // Covers the contiguous {seq, data} region that follows crc32
    return crc32(&b->seq, sizeof(b->seq) + sizeof(b->data));
}

// Write to exactly one file, open then close. Never more than one file open.
static bool store_write(int idx, const process_pair_message_t *value) {
    store_block_t block;
    block.seq  = g_seq;
    block.data = *value;
    block.crc32 = store_block_checksum(&block);

    FILE *f = fopen(g_store_paths[idx], "rb+");
    if (!f) return false;
    bool ok = fwrite(&block, sizeof(block), 1, f) == 1;
    fclose(f);
    return ok;
}

// Read from exactly one file, open then close. Never more than one file open.
static bool store_read(int idx, store_block_t *out) {
    FILE *f = fopen(g_store_paths[idx], "rb");
    if (!f) return false;
    bool ok = fread(out, sizeof(*out), 1, f) == 1;
    fclose(f);
    if (!ok) return false;
    if (out->crc32 != store_block_checksum(out)) return false;
    return true;
}

// Round-robin: write only to file[g_seq % NPLEX], then advance seq.
// A crash mid-write corrupts only that one file; the previous file is intact.
static bool reliable_write(const process_pair_message_t *value) {
    bool ok = store_write(g_seq % NPLEX, value);
    g_seq++;
    return ok;
}

// Read all files one at a time. Use seq as tiebreaker among valid copies.
static bool reliable_read(process_pair_message_t *value) {
    store_block_t blocks[NPLEX];
    bool          valid[NPLEX];
    int           best = -1;

    for (int i = 0; i < NPLEX; i++) {
        valid[i] = store_read(i, &blocks[i]);
        if (valid[i] && (best < 0 || blocks[i].seq > blocks[best].seq))
            best = i;
    }

    if (best < 0) return false;

    *value = blocks[best].data;
    g_seq  = blocks[best].seq + 1;
    return true;
}
// GN

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
        .sin_port        = htons(g_process_pair_port),
    };
    int err = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err != 0 || listen(fd, 1) < 0) {
        close(fd);
        return -1;
    }
    backup_listen_fd = fd;

    // GN
    for (int i = 0; i < NPLEX; i++) {
        FILE *f = fopen(g_store_paths[i], "rb");
        if (f) {
            fclose(f);
        } else {
            f = fopen(g_store_paths[i], "wb");
            if (!f) {
                LOGE(TAG, "failed to create store file %s", g_store_paths[i]);
                close(backup_listen_fd);
                backup_listen_fd = -1;
                return -1;
            }
            fclose(f);
        }
    }
    if (!reliable_read(&committed)) {
        LOGD(TAG, "no valid committed state on disk, starting fresh");
        memset(&committed, 0, sizeof(committed));
    } else {
        LOGD(TAG, "restored committed state from disk");
    }
    // GN
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

    LOGD(TAG, "started, waiting for primary on port %d", g_process_pair_port);
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
        // GN
        reliable_write(&committed);
        // GN
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


