#ifndef ELEVATOR_CONTROL_TASK_H
#define ELEVATOR_CONTROL_TASK_H
#include <pthread.h>

#include <rtsystem/rtsystem_config.h>
#include <rtsystem/messages.h>
#include <rtsystem/core/task_helper.h>

extern const task_config_t elevator_task_config;

typedef struct {
    worldview_t worldview;
    elevator_state_t elevator_state;
    pthread_mutex_t lock;
} local_elevator_t;

extern local_elevator_t my_elevator;

#endif