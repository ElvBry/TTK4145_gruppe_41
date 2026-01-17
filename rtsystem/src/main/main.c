#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <sys/signalfd.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/async_log_helper.h>
#include <rtsystem/tasks/log_task.h>

#define LOG_QUEUE_SIZE 64

#define PRIORITY_LOG_TASK 10

static const char* TAG = "main";

// Shared shutdown flags
volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_sigint_count = 0;

static int sig_fd = -1;

int main(void) {
    // Block SIGINT and use signalfd instead
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    sig_fd = signalfd(-1, &mask, 0);
    if (sig_fd == -1) {
        perror("signalfd");
        return 1;
    }

    if (log_task_init(LOG_QUEUE_SIZE, PRIORITY_LOG_TASK) != 0) {
        fprintf(stderr, "Failed to initialize log task\n");
        close(sig_fd);
        return 1;
    }

    LOGD(TAG, "rtsystem started");

    // Main loop - wait for signals
    struct pollfd pfd = {
        .fd = sig_fd,
        .events = POLLIN,
    };

    while (g_running) {
        int ret = poll(&pfd, 1, -1);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            struct signalfd_siginfo info;
            read(sig_fd, &info, sizeof(info));
            g_sigint_count++;
            g_running = 0;
            printf("\n"); // just for formatting
        }
    }
    LOGD(TAG, "received SIGINT, shutting down...");

    // Stop other tasks here (when implemented)
    // ...

    // Stop log task last so it can drain remaining messages
    log_task_stop();

    // Poll for log_task completion or force kill on second SIGINT
    struct pollfd wait_fds[2] = {
        { .fd = g_log_done_fd, .events = POLLIN },
        { .fd = sig_fd,        .events = POLLIN },
    };

    int ret = poll(wait_fds, 2, 3000);

    if (ret > 0 && (wait_fds[0].revents & POLLIN)) {
        // log_task finished gracefully
    } else if (ret > 0 && (wait_fds[1].revents & POLLIN)) {
        fprintf(stderr, "Forced shutdown\n");
        log_task_cancel();
    } else {
        fprintf(stderr, "Log task timeout, forcing shutdown\n");
        log_task_cancel();
    }

    log_task_join();
    log_task_cleanup();
    close(sig_fd);

    return 0;
}
