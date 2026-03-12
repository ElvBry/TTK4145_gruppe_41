#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/signalfd.h>

#include <rtsystem/core/task_helper.h>
#include <rtsystem/tasks/example_worker_task.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
    #include <rtsystem/tasks/log_task.h>
    #define LOG_QUEUE_SIZE 64
    #define PRIORITY_LOG_TASK 10
    #define LOG_TASK_SHUTDOWN_TIMEOUT_MS 3000
#else
    #include <rtsystem/log_helper.h>
#endif


static const char *TAG = "main";

#define PRIORITY_MAIN 50
#define SYSTEM_TASK_SHUTDOWN_TIMEOUT_MS 3000

#define SYSTEM_TASKS_ARRAY_CAPACITY 3

// Shared global flag for graceful shutdown
volatile sig_atomic_t g_running = 1;

static int sig_fd = -1;

// Queue containing all system tasks, needed for graceful shutdown
static task_array_t system_tasks;

int main(void) {
    // Set main thread priority
    struct sched_param param = { .sched_priority = PRIORITY_MAIN };
    int err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (err != 0) {
        perror("Failed to set main thread priority (try running with sudo)");
        return EXIT_FAILURE;
    }

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

    #ifdef ASYNC_LOG
        // Initialize log task first (special case - not in task_array)
        err = log_task_init(LOG_QUEUE_SIZE, PRIORITY_LOG_TASK);
        if (err != 0) {
            fprintf(stderr, "Failed to initialize log task\n");
            close(sig_fd);
            return EXIT_FAILURE;
        }
    #endif

    LOGD(TAG, "rtsystem started");

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
    // Start process_pair backup if it does not exist

    // Wait for process_pair backup to be finished if it was made

    // Main loop - wait for signals
    struct pollfd pfd = {
        .fd = sig_fd,
        .events = POLLIN,
    };

    while (g_running) {
        // Waits for sig_fd to be set to SIGINT by user ('Ctrl + c' in terminal)
        int ret = poll(&pfd, 1, -1);

        if (ret < 0) {
            LOGE_ERRNO(TAG, "poll failed on signal fd: ");
            break;
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            struct signalfd_siginfo info;
            read(sig_fd, &info, sizeof(info));
            g_running = 0;
            printf("\n");
        }
    }
    // Main loop finished

    LOGD(TAG, "received SIGINT, shutting down...");

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
    return EXIT_SUCCESS;
}
