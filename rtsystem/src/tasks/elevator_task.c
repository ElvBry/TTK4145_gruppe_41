#include "rtsystem/tasks/elevator_task.h"
#include "rtsystem/core/elevator_hardware.h"
#include <unistd.h>

#include <rtsystem/rtsystem_config.h>
#include <rtsystem/core/task_helper.h>
#include <rtsystem/messages.h>

#define LOG_LEVEL LOG_LEVEL_BACKUP_TASK
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

const static char *TAG = "elevator_task";

extern volatile int g_running;

local_elevator_t my_elevator;

static int   elevator_init(task_handle_t *self, void *init_arg);
static void  elevator_cleanup(task_handle_t *self);
static void *elevator_entry(task_handle_t *self);


const task_config_t elevator_task_config = {
    .priority   = PRIORITY_ELEVATOR,
    .on_init    = elevator_init,
    .on_cleanup = elevator_cleanup,
    .entry      = elevator_entry,
    .on_stop    = NULL,
};

static int elevator_init(task_handle_t *self, void *init_arg) {
    (void)init_arg;
    elevator_hardware_init();
    errno = 0;
    return 0;
}

static void  elevator_cleanup(task_handle_t *self) {
    return;
}

static void *elevator_entry(task_handle_t *self) {
    

    while (g_running && self->state != TASK_STATE_STOPPING) {
        // read cab state
        // propose new cab state to primary to be comitted to file by backup
        // update cab and hall states
        usleep(1000 * ELEVATOR_TASK_HEARTBEAT_MS);
    }
    return NULL;
}