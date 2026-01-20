#include <stdlib.h>
#include <unistd.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include <rtsystem/core/task_helper.h>
#include <rtsystem/tasks/example_worker_task.h>
#include <rtsystem/async_log_helper.h>

#define EXAMPLE_WORKER_POLL_TIMEOUT_MS 10

const static char *TAG = "worker_task";

extern volatile int g_running;

static size_t worker_num_counter = 0;

static int   example_worker_init(task_handle_t *self, void *init_arg);
static void  example_worker_cleanup(task_handle_t *self);
static void *example_worker_entry(task_handle_t *self);

const task_config_t worker_task_config = {
    .priority   = DEFAULT_WORK_EXAMPLE_PRIORITY,
    .entry      = example_worker_entry,
    .on_init    = example_worker_init,
    .on_stop    = NULL,
    .on_cleanup = example_worker_cleanup,
};



static int example_worker_init(task_handle_t *self, void *init_arg) {
    const worker_data_t temp_data = *(worker_data_t *)init_arg;
    
    self->name = "test";
    worker_data_t *data = malloc(sizeof(worker_data_t));
    if (data == NULL) {
        LOGE(TAG, "malloc failed for worker_data_t");
        return -1;
    }
    
    data->time_to_live_ms    = temp_data.time_to_live_ms;
    data->msg_send_period_ms = temp_data.msg_send_period_ms;
    data->msg_len            = temp_data.msg_len;
    data->message            = temp_data.message;

    self->task_resources = data;
    return 0;
}

static void example_worker_cleanup(task_handle_t *self) {
    if (self == NULL) return;
    worker_data_t *data = self->task_resources;
    if (data != NULL) {
        free(data->message);
        free(data);
        LOGD(TAG, "%s : freed message", self->name);
    }
}

// Simple worker task example that outputs a message every period
// Exits when stopped or when its time to live is finished.
static void *example_worker_entry(task_handle_t *self) {
    // Capture data and store, make sure to deallocate data from pointers
    worker_data_t *my_data = (worker_data_t *) self->task_resources;
    const size_t my_TTL_ms    = my_data->time_to_live_ms;
    const size_t my_period_ms = my_data->msg_send_period_ms;
    const size_t my_msg_len   = my_data->msg_len;
    const char * my_msg       = my_data->message;

    // Optional, just for keeping track of multiple instances of same task
    worker_num_counter++;

    self->state = TASK_STATE_RUNNING;
    size_t counter_ms = 0;
    size_t prev_message_timestamp_ms = 0;
    while (counter_ms < my_TTL_ms && g_running && self->state != TASK_STATE_STOPPING) {
        // If waiting on IO, use poll like shown in stdin or dispatcher instead
        usleep(EXAMPLE_WORKER_POLL_TIMEOUT_MS * 1000);
        counter_ms += EXAMPLE_WORKER_POLL_TIMEOUT_MS;
        if (prev_message_timestamp_ms + my_period_ms <= counter_ms) {
            LOGI(TAG, "%s : %s", self->name, my_msg);
            prev_message_timestamp_ms = counter_ms;
        }
    }
    //example_worker_cleanup(self);
    LOGD(TAG, "%s : exiting...", self->name);
    task_handle_mark_done(self);
    return NULL;
}

