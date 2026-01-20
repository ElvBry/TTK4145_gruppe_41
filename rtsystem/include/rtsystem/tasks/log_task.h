#ifndef LOG_TASK_H
#define LOG_TASK_H

#include <signal.h>
#include <stddef.h>

// Colors for log levels
#define COLOR_CYAN   "\033[36m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RED    "\033[31m"
#define COLOR_RESET  "\033[0m"

// Colors for tags (256-color palette) - add more colors here to reduce hash collisions
#define TAG_COLOR_PURPLE     "\033[38;5;141m"
#define TAG_COLOR_ORANGE     "\033[38;5;179m"
#define TAG_COLOR_TEAL       "\033[38;5;109m"
#define TAG_COLOR_PINK       "\033[38;5;175m"
#define TAG_COLOR_LIME       "\033[38;5;149m"
#define TAG_COLOR_BLUE       "\033[38;5;74m"
#define TAG_COLOR_LAVENDER   "\033[38;5;183m"
#define TAG_COLOR_PEACH      "\033[38;5;216m"
#define TAG_COLOR_MINT       "\033[38;5;121m"
#define TAG_COLOR_CORAL      "\033[38;5;210m"
#define TAG_COLOR_SKY        "\033[38;5;117m"
#define TAG_COLOR_CHERRY     "\033[38;5;125m"
#define TAG_COLOR_RASPBERRY  "\033[38;5;162m"  
#define TAG_COLOR_TAN        "\033[38;5;179m"
#define TAG_COLOR_FOREST     "\033[38;5;64m"
#define TAG_COLOR_AZURE      "\033[38;5;69m"
#define TAG_COLOR_COBALT     "\033[38;5;62m"
#define TAG_COLOR_BRICK      "\033[38;5;131m"
#define TAG_COLOR_PLUM       "\033[38;5;96m"
#define TAG_COLOR_SEAFOAM    "\033[38;5;122m"
#define TAG_COLOR_LILAC      "\033[38;5;147m"
#define TAG_COLOR_SALMON     "\033[38;5;209m"
#define TAG_COLOR_MUSTARD    "\033[38;5;172m"
#define TAG_COLOR_OCEAN      "\033[38;5;30m"
#define TAG_COLOR_FUCHSIA    "\033[38;5;198m"
#define TAG_COLOR_AQUA       "\033[38;5;51m"
#define TAG_COLOR_CHARTREUSE "\033[38;5;118m"
#define TAG_COLOR_CHARCOAL   "\033[38;5;235m"  
#define TAG_COLOR_EBONY      "\033[38;5;234m"
#define TAG_COLOR_DEEPRED    "\033[38;5;88m"
#define TAG_COLOR_DEEPGREEN  "\033[38;5;22m"   
#define TAG_COLOR_DEEPBLUE   "\033[38;5;17m"

// Shared shutdown flags (defined in main.c)
extern volatile sig_atomic_t g_running;
extern volatile sig_atomic_t g_sigint_count;

// Log task shutdown flag (defined in log_task.c)
// Set to 0 after all other tasks have stopped to allow final log drain
extern volatile int g_log_running;

// Event fd signaled when log_task exits (for poll-based waiting)
extern int g_log_done_fd;

// Initialize log queue and start log task thread
// queue_size: number of log messages the queue can hold
// priority: thread priority (0 for default, or SCHED_FIFO/RR priority 1-99)
// Returns 0 on success, -1 on error
int log_task_init(const size_t queue_size, const int priority);

// Signal log task to stop
static inline void log_task_stop(void) {
    g_log_running = 0;
}

// Wait for log task thread to finish
void log_task_join(void);

// Force cancel log task thread (use only if stuck)
void log_task_cancel(void);

// Cleanup log queue. Call after log_task_join.
void log_task_cleanup(void);

#endif
