#include <stdlib.h>
#include <unistd.h>

#include <rtsystem/core/task_helper.h>
#include <rtsystem/tasks/process_pair_primary_task.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

const static char *TAG = "primary_task";

#define APP_TASK_SHUTDOWN_TIMEOUT_MS 2000

extern volatile int g_running;

char *reason = "temporary reason for shutdown, add logic later";

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

// Currently no use for init arg
static int process_pair_primary_init(task_handle_t *self, void *init_arg) {
    // Check if primary socket already exists, if it does return -1

    int err = task_array_init(&application_tasks, APPLICATION_TASKS_ARRAY_CAPACITY);
    if (err != 0) {
        LOGE(TAG, "failed to initialize system tasks array");
        return -1;
    }
    // Create tasks needed for program
    
    return 0;
}

static void process_pair_primary_cleanup(task_handle_t *self) {
    task_array_stop_all(&application_tasks);
    int err = task_array_poll_all(&application_tasks, -1, APP_TASK_SHUTDOWN_TIMEOUT_MS);
    if (err != 0) {
        task_array_cancel_all(&application_tasks);
    }
    task_array_join_all(&application_tasks);
    task_array_destroy_all(&application_tasks);
}

static void *process_pair_primary_entry(task_handle_t *self) {
    while (g_running && self->state != TASK_STATE_STOPPING) {
        // add process pair logic here
        usleep(10000);
    }
    LOGD(TAG, "received shutdown signal with reason: %s", reason);
    process_pair_primary_cleanup(self);
    LOGD(TAG, "exiting...");
    task_handle_mark_done(self);
    return NULL;
}