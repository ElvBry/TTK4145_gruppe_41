#include <stdio.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>


#include <rtsystem/rtsystem_config.h>
#define LOG_LEVEL LOG_LEVEL_LOG_TASK
#include <rtsystem/tasks/log_task.h>
#include <rtsystem/async_log_helper.h>


static const char *TAG = "log_task";

#define LOG_TIME_RESOLUTION_NS 1000
#define LOG_TAG_MIN_WIDTH 14

// Helper macros for stringifying LOG_TAG_MIN_WIDTH in format string
#define STR(x) #x
#define XSTR(x) STR(x)

static const char* tag_colors[] = {
    TAG_COLOR_PURPLE,
    TAG_COLOR_ORANGE,
    TAG_COLOR_TEAL,
    TAG_COLOR_PINK,
    TAG_COLOR_LIME,
    TAG_COLOR_BLUE,
    TAG_COLOR_LAVENDER,
    TAG_COLOR_PEACH,
    TAG_COLOR_MINT,
    TAG_COLOR_CORAL,
    TAG_COLOR_SKY,
    TAG_COLOR_CHERRY,
    TAG_COLOR_RASPBERRY,
    TAG_COLOR_TAN,
    TAG_COLOR_FOREST,
    TAG_COLOR_AZURE,
    TAG_COLOR_COBALT,
    TAG_COLOR_BRICK,
    TAG_COLOR_PLUM,
    TAG_COLOR_SEAFOAM,
    TAG_COLOR_LILAC,
    TAG_COLOR_SALMON,
    TAG_COLOR_MUSTARD,
    TAG_COLOR_OCEAN,
    TAG_COLOR_FUCHSIA,
    /*TAG_COLOR_AQUA,
    TAG_COLOR_CHARTREUSE,
    TAG_COLOR_CHARCOAL,
    TAG_COLOR_EBONY,
    TAG_COLOR_DEEPRED,
    TAG_COLOR_DEEPGREEN,
    TAG_COLOR_DEEPBLUE,*/
};

// Creates small hash to coloured tags for easier reading
static const char* get_tag_color(const char* tag) {
    unsigned hash = 0;
    while (*tag) {
        hash = hash * 31 + (unsigned char)*tag++;
    }
    return tag_colors[hash % (sizeof(tag_colors) / sizeof(tag_colors[0]))];
}

// Global log queue (declared in async_log_helper.h)
fifo_queue_t g_log_queue;

// Log task status flag (used by ALOG macro guard)
volatile int g_log_running = 1;

// Event fd signaled to request log_task shutdown
int g_log_shutdown_fd = -1;

// Event fd signaled when log_task exits
int g_log_done_fd = -1;

// Internal thread handle
static pthread_t log_thread;

static void print_log_message(const log_message_t* msg) {
    static const char* colors[] = {COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW, COLOR_RED};
    static const char* levels[] = {"D", "I", "W", "E"};

    struct tm tm;
    localtime_r(&msg->timestamp.tv_sec, &tm);

    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d", (int)getpid());

    char line[512];
    int len = snprintf(line, sizeof(line),
            "[%s%s" COLOR_RESET "|%-7s] %02d:%02d:%02d.%06ld %s%s %s%-" XSTR(LOG_TAG_MIN_WIDTH) "s%s: %s" COLOR_RESET "\n",
            get_tag_color(pid_str), pid_str,
            msg->role,
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            msg->timestamp.tv_nsec / LOG_TIME_RESOLUTION_NS,
            colors[msg->level],
            levels[msg->level],
            get_tag_color(msg->tag),
            msg->tag,
            colors[msg->level],
            msg->message);
    write(STDERR_FILENO, line, len);
}

static void* log_task(void* arg) {
    (void)arg;
    log_message_t msg;

    LOGD(TAG, "successfully initialized. Logging queue...");

    struct pollfd pfds[2] = {
        { .fd = g_log_queue.event_fd,  .events = POLLIN },
        { .fd = g_log_shutdown_fd,     .events = POLLIN },
    };

    for (;;) {
        int err = poll(pfds, 2, -1);

        if (err == -1) {
            fprintf(stderr, "%s : could not poll log queue: %s", TAG, strerror(errno));
            errno = 0;
            continue;
        }

        // Shutdown requested
        if (pfds[1].revents & POLLIN) {
            break;
        }

        if (pfds[0].revents & POLLIN) {
            err = fifo_queue_receive(&g_log_queue, &msg);
            if (err != 0) {
                LOGW(TAG, "should not happen, check cause");
                continue;
            }
            print_log_message(&msg);
        }
    }

    // Drain remaining messages
    g_log_running = 1;
    LOGD(TAG, "received shutdown signal, draining remaining messages...");
    g_log_running = 0;
    while (fifo_queue_receive(&g_log_queue, &msg) == 0) {
        print_log_message(&msg);
    }

    // Signal completion
    uint64_t done = 1;
    write(g_log_done_fd, &done, sizeof(done));

    return NULL;
}

int log_task_init(const size_t queue_size, const int priority) {
    int err = fifo_queue_init(&g_log_queue, sizeof(log_message_t), queue_size);
    if (err != 0) {
        perror("log_task_init: fifo_queue_init");
        return -1;
    }

    g_log_shutdown_fd = eventfd(0, 0);
    if (g_log_shutdown_fd == -1) {
        perror("log_task_init: eventfd (shutdown)");
        fifo_queue_destroy(&g_log_queue);
        return -1;
    }

    g_log_done_fd = eventfd(0, 0);
    if (g_log_done_fd == -1) {
        perror("log_task_init: eventfd (done)");
        close(g_log_shutdown_fd);
        fifo_queue_destroy(&g_log_queue);
        return -1;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (priority > 0) {
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        struct sched_param param = { .sched_priority = priority };
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    err = pthread_create(&log_thread, &attr, log_task, NULL);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        fprintf(stderr, "log_task_init: pthread_create: %s\n", strerror(err));
        close(g_log_done_fd);
        close(g_log_shutdown_fd);
        fifo_queue_destroy(&g_log_queue);
        return -1;
    }

    return 0;
}

void log_task_join(void) {
    pthread_join(log_thread, NULL);
}

void log_task_cancel(void) {
    pthread_cancel(log_thread);
}

void log_task_cleanup(void) {
    fifo_queue_destroy(&g_log_queue);
    if (g_log_shutdown_fd != -1) {
        close(g_log_shutdown_fd);
        g_log_shutdown_fd = -1;
    }
    if (g_log_done_fd != -1) {
        close(g_log_done_fd);
        g_log_done_fd = -1;
    }
}
