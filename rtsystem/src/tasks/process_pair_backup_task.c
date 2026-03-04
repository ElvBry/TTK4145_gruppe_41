
#include <rtsystem/core/task_helper.h>
#include <rtsystem/tasks/process_pair_primary_task.h>


#define LOG_LEVEL LOG_LEVEL_DEBUG
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

extern volatile int g_running;



static int   process_pair_backup_init(task_handle_t *self, void *init_arg);
static void  process_pair_backup_cleanup(task_handle_t *self);
static void *process_pair_backup_entry(task_handle_t *self);


const task_config_t backup_task_config = {
    .priority   = DEFAULT_BACKUP_PRIORITY,
    .on_init    = process_pair_backup_init,
    .entry      = process_pair_backup_entry,
    .on_cleanup = process_pair_backup_cleanup,
    .on_stop    = NULL,
};


static int process_pair_backup_init(task_handle_t *self, void *init_arg){
    // set up UDL connection to primary
    // check if log file exists, otherwise create it

}
static void process_pair_backup_cleanup(task_handle_t *self){
    
}
static void *process_pair_backup_entry(task_handle_t *self){
    // Try to recvfrom from primary
    // if timeout, break out of loop and run cleanup and exit gracefully
    while (g_running && self->state != TASK_STATE_STOPPING) {
        
        
        // recvfrom primary with timeout 
        // if timeout, break out of loop and run cleanup and exit gracefully
    }



}


