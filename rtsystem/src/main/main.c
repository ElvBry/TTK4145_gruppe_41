#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/signalfd.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/async_log_helper.h>
#include <rtsystem/core/task_helper.h>
#include <rtsystem/tasks/log_task.h>
#include <rtsystem/tasks/stdin_task.h>
#include <rtsystem/tasks/dispatcher_task.h>
#include <rtsystem/tasks/example_worker_task.h>

#define LOG_QUEUE_SIZE 64
#define STDIN_LINE_BUF_SIZE 256
#define DISPATCH_QUEUE_SIZE 8

#define PRIORITY_MAIN 50
#define PRIORITY_LOG_TASK 10

#define TASK_SHUTDOWN_TIMEOUT_MS 1000
#define LOG_TASK_SHUTDOWN_TIMEOUT_MS 3000

#define SYSTEM_TASKS_ARRAY_CAPACITY 3

static const char *TAG = "main";

// Shared global flag for graceful shutdown
volatile sig_atomic_t g_running = 1;

static int sig_fd = -1;

// System tasks array for stdin and dispatcher
static task_array_t g_system_tasks;

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

    // Initialize log task first (special case - not in task_array)
    err = log_task_init(LOG_QUEUE_SIZE, PRIORITY_LOG_TASK); 
    if (err != 0) {
        fprintf(stderr, "Failed to initialize log task\n");
        close(sig_fd);
        return EXIT_FAILURE;
    }

    LOGD(TAG, "rtsystem started");

    // Initialize system tasks array
    err = task_array_init(&g_system_tasks, SYSTEM_TASKS_ARRAY_CAPACITY);
    if (err != 0) {
        LOGE(TAG, "failed to initialize system tasks array");
        log_task_stop();
        log_task_join();
        log_task_cleanup();
        close(sig_fd);
        return EXIT_FAILURE;
    }

    // Create system tasks
    size_t stdin_buf_size = STDIN_LINE_BUF_SIZE;
    size_t dispatch_queue_size = DISPATCH_QUEUE_SIZE;

    if (task_create(&g_system_tasks, &stdin_task_config, &stdin_buf_size, "stdin_task") == NULL) {
        LOGE(TAG, "failed to create stdin_task");
    }

    if (task_create(&g_system_tasks, &dispatcher_task_config, &dispatch_queue_size, "disp_task") == NULL) {
        LOGE(TAG, "failed to create dispatcher_task");
    }
    // Example task that helps understand functionality
    char *temp = "I AM A SURGEON";
    const size_t msg_len = strlen(temp) + 1;
    
    char *message = (char *) malloc(msg_len * sizeof(char));
    if (message == NULL) {
        LOGE(TAG, "could not allocate example message");
        return EXIT_FAILURE;
    }
    strcpy(message, temp);
    const worker_data_t worker_data = {
        .time_to_live_ms = 3600,
        .msg_send_period_ms = 800,
        .msg_len = msg_len,
        .message = message,
    };

    if (task_create(&g_system_tasks, &worker_task_config, (void *)&worker_data, "wrk_task0") == NULL) {
        LOGE(TAG, "failed to create example_worker_task");
    }
    

    // Main loop - wait for signals
    struct pollfd pfd = {
        .fd = sig_fd,
        .events = POLLIN,
    };

    while (g_running) {
        // Waits for sig_fd to be set, happens on 
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
    task_array_stop_all(&g_system_tasks);

    // Poll for task completion or force cancel on timeout/second SIGINT
    int ret = task_array_poll_all(&g_system_tasks, sig_fd, TASK_SHUTDOWN_TIMEOUT_MS);
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
        task_array_cancel_all(&g_system_tasks);
    }        
    
    // Join and destroy all system tasks
    task_array_join_all(&g_system_tasks);
    task_array_destroy_all(&g_system_tasks);
    task_array_destroy(&g_system_tasks);

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
    close(sig_fd);

    return EXIT_SUCCESS;
}
