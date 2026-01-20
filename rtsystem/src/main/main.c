#include "rtsystem/tasks/dispatcher_task.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <sys/signalfd.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/async_log_helper.h>
#include <rtsystem/tasks/log_task.h>
#include <rtsystem/tasks/stdin_task.h>
#include <rtsystem/tasks/dispatcher_task.h>

#define LOG_QUEUE_SIZE 64
#define STDIN_LINE_BUF_SIZE 256
#define DISPATCH_QUEUE_SIZE 8

// Set priority of task (should not exceed 50)
#define PRIORITY_MAIN 50 // Should be highest in order to catch sigint and cleanly shut down all other tasks
#define PRIORITY_DISPATCH_TASK 40
#define PRIORITY_STDIN_TASK 12
#define PRIORITY_LOG_TASK   10

#define TASK_SHUTDOWN_TIMEOUT_MS 1000

static const char *TAG = "main";

// Shared shutdown flags
volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_sigint_count = 0;

static int sig_fd = -1;

// Array of all task arrays for unified shutdown
static task_array_t* task_arrays[] = {
    &g_stdin_task,
    &g_dispatcher_task,
    // Add more task arrays here as needed
};
static const size_t num_task_arrays = sizeof(task_arrays) / sizeof(task_arrays[0]);

int main(void) {
    struct sched_param param;
    param.sched_priority = PRIORITY_MAIN;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
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

    if (log_task_init(LOG_QUEUE_SIZE, PRIORITY_LOG_TASK) != 0) {
        fprintf(stderr, "Failed to initialize log task\n");
        close(sig_fd);
        return EXIT_FAILURE;
    }

    LOGD(TAG, "rtsystem started");

    // Initialize other tasks
    int err = stdin_task_init(STDIN_LINE_BUF_SIZE, PRIORITY_STDIN_TASK);
    if (err != 0) {
        LOGE(TAG, "failed to initialize stdin_task");
    }
    err = dispatcher_task_init(DISPATCH_QUEUE_SIZE, PRIORITY_DISPATCH_TASK);
    if (err != 0) {
        LOGE(TAG, "failed to initialize dispatcher_task");
    }

    // Main loop - wait for signals
    struct pollfd pfd = {
        .fd = sig_fd,
        .events = POLLIN,
    };

    while (g_running) {
        int ret = poll(&pfd, 1, -1);

        if (ret < 0) {
            LOGE_ERRNO(TAG, "Failed to poll signal file descriptor, SHOULD NOT HAPPEN: ");
            return EXIT_FAILURE;
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            struct signalfd_siginfo info;
            read(sig_fd, &info, sizeof(info));
            g_sigint_count++;
            g_running = 0;
            printf("\n");
        }
    }
    LOGD(TAG, "received SIGINT, shutting down...");

    // Stop all tasks (except log_task)
    for (size_t i = 0; i < num_task_arrays; i++) {
        task_array_stop_all(task_arrays[i]);
    }

    // Poll for task completion or force cancel on second SIGINT
    bool force_cancel = false;
    for (size_t i = 0; i < num_task_arrays; i++) {
        if (force_cancel) {
            task_array_cancel_all(task_arrays[i]);
            continue;
        }

        int ret = task_array_poll_all(task_arrays[i], sig_fd, TASK_SHUTDOWN_TIMEOUT_MS);
        if (ret == -2) {
            LOGW(TAG, "forced shutdown, cancelling all tasks");
            force_cancel = true;
            task_array_cancel_all(task_arrays[i]);
        } else if (ret == -1) {
            LOGW(TAG, "task array %zu timeout, cancelling", i);
            task_array_cancel_all(task_arrays[i]);
        }
    }

    // Join and destroy all tasks
    for (size_t i = 0; i < num_task_arrays; i++) {
        task_array_join_all(task_arrays[i]);
        task_array_handles_destroy_all(task_arrays[i]);
        task_array_destroy(task_arrays[i]);
    }

    // Stop log task last so it can drain remaining messages
    log_task_stop();

    // Poll for log_task completion or force kill on second SIGINT
    struct pollfd log_wait_fds[2] = {
        { .fd = g_log_done_fd, .events = POLLIN },
        { .fd = sig_fd,        .events = POLLIN },
    };

    int ret = poll(log_wait_fds, 2, 3000);

    if (ret < 0) {
        perror("Failed to poll log file descriptor, SHOULD_NOT_HAPPEN: ");
        return 0;
    }

    if (ret == 0) {
        fprintf(stderr, "log_task timeout, forcing shutdown\n");
        log_task_cancel();
    } else if (log_wait_fds[0].revents & POLLIN) {
        // log_task finished gracefully
    } else if (log_wait_fds[1].revents & POLLIN) {
        fprintf(stderr, "Forced log shutdown\n");
        log_task_cancel();
    } else {
        fprintf(stderr, "ILLEGAL STATE, SHOULD NOT HAPPEN\n");
    }

    log_task_join();
    log_task_cleanup();
    close(sig_fd);

    return EXIT_SUCCESS;
}
