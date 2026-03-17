#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/eventfd.h>

#include <rtsystem/rtsystem_config.h>
#include <rtsystem/core/task_helper.h>
#include "rtsystem/tasks/process_pair_backup_task.h"
#include "rtsystem/tasks/process_pair_primary_task.h"
#include <rtsystem/messages.h>
#include <rtsystem/core/elevator_network.h>

#define LOG_LEVEL LOG_LEVEL_MAIN
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
    #include <rtsystem/tasks/log_task.h>
#else
    #include <rtsystem/log_helper.h>
#endif


static const char *TAG = "main";

// Shared global flag for graceful shutdown
volatile sig_atomic_t g_running = 1;

// Current role of this process, shown in every log line.
// Starts as "backup" (every process tries to bind the port first).
// Updated to "primary" when the backup task promotes.
char *g_process_role = "backup";

// Written to by the backup task to signal main that it should promote to primary
int g_promote_fd = -1;

// Written to by the backup task to signal main that it should initiate shutdown.
// This is used when primary sends PP_MSG_SHUTDOWN to backup — backup writes here
// to wake main's poll loop instead of sending a signal to itself.
int g_shutdown_fd = -1;

static int sig_fd = -1;

// Queue containing all system tasks, needed for graceful shutdown
static task_array_t system_tasks;

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--id") == 0) {
            g_elevator_id = atoi(argv[i + 1]);
            break;
        }
    }
    if (g_elevator_id < 0 || g_elevator_id >= N_ELEVATORS) {
        fprintf(stderr, "usage: rtsystem --id <0..%d>\n", N_ELEVATORS - 1);
        return EXIT_FAILURE;
    }
    g_process_pair_port = PROCESS_PAIR_BASE_PORT + g_elevator_id;
    // Set main thread priority
    struct sched_param param = { .sched_priority = PRIORITY_MAIN };
    int err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (err != 0) {
        perror("failed to set main thread priority");
        fprintf(stderr, "hint: run once after each build: make -C build setcap\n");
        return EXIT_FAILURE;
    }

    // SIGPIPE: when the remote end of a TCP connection closes, the next send()
    // would raise SIGPIPE whose default action is to terminate the process.
    // We ignore it so send() returns -1/EPIPE instead, which we handle explicitly.
    signal(SIGPIPE, SIG_IGN);

    // SIGHUP: sent to all processes in a process group when the group becomes
    // orphaned (i.e. when our parent process dies). We ignore it so a promoted
    // backup survives its parent (the old primary) being killed.
    signal(SIGHUP, SIG_IGN);


    // Block SIGINT and use signalfd instead
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    sig_fd = signalfd(-1, &mask, 0);
    if (sig_fd == -1) {
        perror("signalfd");
        return EXIT_FAILURE;
    }

    g_promote_fd = eventfd(0, 0);
    if (g_promote_fd == -1) {
        perror("eventfd");
        close(sig_fd);
        return EXIT_FAILURE;
    }

    g_shutdown_fd = eventfd(0, 0);
    if (g_shutdown_fd == -1) {
        perror("eventfd (shutdown)");
        close(g_promote_fd);
        close(sig_fd);
        return EXIT_FAILURE;
    }

    #ifdef ASYNC_LOG
        // Initialize log task first (special case - not in task_array)
        err = log_task_init(LOG_QUEUE_SIZE, PRIORITY_LOG_TASK);
        if (err != 0) {
            fprintf(stderr, "failed to initialize log task\n");
            close(sig_fd);
            return EXIT_FAILURE;
        }
    #endif

    // Initialize system tasks array
    err = task_array_init(&system_tasks, SYSTEM_TASKS_ARRAY_CAPACITY);
    if (err != 0) {
        LOGE(TAG, "failed to initialize system tasks array");
        #ifdef ASYNC_LOG
            log_task_stop();
            log_task_join();
            log_task_cleanup();
        #endif
        close(sig_fd);
        return EXIT_FAILURE;
    }

    // Try to start as backup (bind on the process-pair port).
    // If the port is already taken, a backup is already running — connect as primary instead.
    task_handle_t *handle = task_create(&system_tasks, &backup_task_config, NULL, "backup");
    if (handle != NULL) {
        LOGD(TAG, "rtsystem started as backup");
    } else {
        handle = task_create(&system_tasks, &primary_task_config, NULL, "primary");
        if (handle == NULL) {
            LOGE(TAG, "failed to start as primary");
            return EXIT_FAILURE;
        }
        LOGD(TAG, "rtsystem started as primary");
    }

    // Main loop - wait for SIGINT, backup promotion request, or backup shutdown request.
    //
    // Three events can fire:
    //   sig_fd        — SIGINT from the user (Ctrl-C): begin graceful shutdown.
    //   g_promote_fd  — backup task detected primary is gone: switch to primary role.
    //   g_shutdown_fd — backup task received PP_MSG_SHUTDOWN from primary: also shut down.
    struct pollfd pfds[3] = {
        { .fd = sig_fd,        .events = POLLIN },
        { .fd = g_promote_fd,  .events = POLLIN },
        { .fd = g_shutdown_fd, .events = POLLIN },
    };

    while (g_running) {
        int ret = poll(pfds, 3, -1);

        if (ret < 0) {
            LOGE_ERRNO(TAG, "poll failed on signal fd: ");
            break;
        }

        if (pfds[0].revents & POLLIN) {
            struct signalfd_siginfo info;
            read(sig_fd, &info, sizeof(info));
            g_running = 0;
            printf("\n");
        }

        if ((pfds[1].revents & POLLIN) && g_running) {
            uint64_t v;
            eventfd_read(g_promote_fd, &v);
            LOGD(TAG, "backup promoting to primary...");

            // Backup has already called task_handle_mark_done; wait to confirm then clean up
            ret = task_array_poll_all(&system_tasks, sig_fd, SYSTEM_TASK_SHUTDOWN_TIMEOUT_MS);
            if (ret < 0) {
                LOGW(TAG, "backup task did not finish cleanly during promotion, cancelling");
                task_array_cancel_all(&system_tasks);
            }
            task_array_join_all(&system_tasks);
            task_array_destroy_all(&system_tasks);

            // Pass last committed state to primary so it can restore after restart
            process_pair_message_t committed = process_pair_backup_get_last_committed();
            task_handle_t *h = task_create(&system_tasks, &primary_task_config, &committed, "primary");
            if (h == NULL) {
                LOGE(TAG, "failed to create primary task after promotion");
                g_running = 0;
            } else {
                g_process_role = "primary";
                LOGD(TAG, "promoted to primary (pid %d) — stop with: pkill -2 rtsystem", (int)getpid());
            }
        }

        if ((pfds[2].revents & POLLIN) && g_running) {
            uint64_t v;
            eventfd_read(g_shutdown_fd, &v);
            LOGD(TAG, "primary sent shutdown order, initiating graceful exit...");
            g_running = 0;
        }
    }
    // Main loop finished

    LOGD(TAG, "shutting down...");

    // Stop all system tasks
    task_array_stop_all(&system_tasks);

    // Poll for task completion or force cancel on timeout/second SIGINT
    int ret = task_array_poll_all(&system_tasks, sig_fd, SYSTEM_TASK_SHUTDOWN_TIMEOUT_MS);
    if (ret >= 0) {
        LOGD(TAG, "all tasks finished and ready to be joined");
    } else {
        switch (ret) {
            case -1:
                LOGW(TAG, "shutdown timeout, cancelling tasks");
                break;
            case -2:
                LOGW(TAG, "forced shutdown requested, cancelling tasks");
                break;
            case -3:
                LOGE(TAG, "poll error during shutdown, cancelling tasks");
                break;
            default:
                LOGE(TAG, "in default branch of poll - SHOULD NOT HAPPEN");
                return EXIT_FAILURE;      
        }
        task_array_cancel_all(&system_tasks);
    }        
    
    // Join and destroy all system tasks
    task_array_join_all(&system_tasks);
    task_array_destroy_all(&system_tasks);
    task_array_destroy(&system_tasks);

    #ifdef ASYNC_LOG
        LOGD(TAG, "stopping log task");
        // Stop log task last so it can drain remaining messages
        log_task_stop();

        struct pollfd log_wait_fds[2] = {
            { .fd = g_log_done_fd, .events = POLLIN },
            { .fd = sig_fd,        .events = POLLIN },
        };

        ret = poll(log_wait_fds, 2, LOG_TASK_SHUTDOWN_TIMEOUT_MS);

        if (ret < 0) {
            perror("poll failed on log done fd");
        } else if (ret == 0) {
            fprintf(stderr, "log_task timeout, forcing shutdown\n");
            log_task_cancel();
        } else if (log_wait_fds[0].revents & POLLIN) {
            // log_task finished gracefully
        } else if (log_wait_fds[1].revents & POLLIN) {
            fprintf(stderr, "Forced log shutdown\n");
            log_task_cancel();
        }

        log_task_join();
        log_task_cleanup();
    #endif

    close(sig_fd);
    close(g_promote_fd);
    close(g_shutdown_fd);
    return EXIT_SUCCESS;
}
