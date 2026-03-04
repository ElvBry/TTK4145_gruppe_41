
#include <rtsystem/core/task_helper.h>
#include <rtsystem/tasks/example_worker_task.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif


static int   process_pair_primary_init(task_handle_t *self, void *init_arg);
static void  process_pair_primary_cleanup(task_handle_t *self);
static void *process_pair_primary_entry(task_handle_t *self);

static task_array_t application_tasks;

const task_config_t primary_task_config = {
    .priority   = DEFAULT_PRIMARY_PRIORITY,
    .entry      = process_pair_primary_entry,
    .on_init    = process_pair_primary_init,
    .on_stop    = NULL,
    .on_cleanup = process_pair_primary_cleanup,
};


static int process_pair_primary_entry()