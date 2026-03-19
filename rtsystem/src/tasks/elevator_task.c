#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "rtsystem/tasks/elevator_task.h"
#include "rtsystem/tasks/elevator_fsm.h"
#include "rtsystem/tasks/elevator_roles.h"
#include "rtsystem/core/elevator_hardware.h"
#include "rtsystem/core/elevator_network.h"
#include <rtsystem/rtsystem_config.h>
#include <rtsystem/core/task_helper.h>
#include <rtsystem/messages.h>

#define LOG_LEVEL LOG_LEVEL_BACKUP_TASK
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

static const char *TAG = "elevator_task";

extern volatile int g_running;

local_elevator_t my_elevator = {0};

static void restore_state(const process_pair_message_t *committed) {
    pthread_mutex_lock(&my_elevator.lock);
    memcpy(my_elevator.elevator_state.cab_requests,
           committed->my_elevator_state.cab_requests,
           sizeof(my_elevator.elevator_state.cab_requests));
    my_elevator.elevator_state.behaviour = committed->my_elevator_state.behaviour;
    my_elevator.worldview = committed->worldview;
    if (committed->my_elevator_state.behaviour == EB_DOOR_OPEN) {
        if (my_elevator.elevator_state.floor == committed->my_elevator_state.floor)
            door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
        else
            my_elevator.elevator_state.behaviour = EB_IDLE;
    }
    pthread_mutex_unlock(&my_elevator.lock);
}

static void elevator_startup_sequence(void) {
    LOGI(TAG, "startup sequence: moving down to find floor sensor...");
    int8_t current_floor = elevator_hardware_get_floor_sensor_signal();
    const int total_ticks = ELEVATOR_STARTUP_MAX_CYCLES * ELEVATOR_STARTUP_TICKS_BETWEEN_FLOORS_MS;

    for (int i = 0; i < total_ticks && current_floor == -1 && g_running; i++) {
        pthread_mutex_lock(&my_elevator.lock);
        my_elevator.elevator_state.behaviour = EB_MOVING;
        pthread_mutex_unlock(&my_elevator.lock);
        elevator_hardware_set_motor_direction(DIRN_DOWN);
        usleep(1000 * ELEVATOR_TASK_HEARTBEAT_MS);
        current_floor = elevator_hardware_get_floor_sensor_signal();
    }

    elevator_hardware_set_motor_direction(DIRN_STOP);

    if (!g_running) return;

    if (current_floor == -1) {
        LOGE(TAG, "startup sequence: no floor found after %d ticks (~%d ms) — "
                  "elevator may be out of bounds or motor is cut. Shutting down.",
             total_ticks, total_ticks * ELEVATOR_TASK_HEARTBEAT_MS);
        g_running = 0;
        return;
    }

    pthread_mutex_lock(&my_elevator.lock);
    my_elevator.elevator_state.floor     = current_floor;
    my_elevator.elevator_state.dirn      = DIRN_STOP;
    my_elevator.elevator_state.behaviour = EB_IDLE;
    elevator_hardware_set_motor_direction(DIRN_STOP);
    pthread_mutex_unlock(&my_elevator.lock);
    LOGI(TAG, "startup sequence done, at floor %d", current_floor);
}

void read_update_cab_state(void) {
    pthread_mutex_lock(&my_elevator.lock);
    for (int f = 0; f < N_FLOORS; f++)
        my_elevator.elevator_state.cab_requests[f] |= elevator_hardware_get_button_signal(BUTTON_CAB, f);
    my_elevator.elevator_state.floor = elevator_hardware_get_floor_sensor_signal();
    pthread_mutex_unlock(&my_elevator.lock);
}

void write_elevator_state(void) {
    pthread_mutex_lock(&my_elevator.lock);

    // Motor runs only when actively moving — dirn carries travel direction semantics
    // and must not drive the motor while the door is open or the elevator is idle.
    elevator_hardware_motor_direction_t safe_dirn = DIRN_STOP;
    if (my_elevator.elevator_state.behaviour == EB_MOVING) {
        safe_dirn = my_elevator.elevator_state.dirn;
        if (safe_dirn == DIRN_DOWN && my_elevator.elevator_state.floor == 0)
            safe_dirn = DIRN_STOP;
        if (safe_dirn == DIRN_UP && my_elevator.elevator_state.floor == N_FLOORS - 1)
            safe_dirn = DIRN_STOP;
    }
    elevator_hardware_set_motor_direction(safe_dirn);
    elevator_hardware_set_door_open_lamp(my_elevator.elevator_state.behaviour == EB_DOOR_OPEN);

    if (my_elevator.elevator_state.floor != -1)
        elevator_hardware_set_floor_indicator(my_elevator.elevator_state.floor);

    for (int f = 0; f < N_FLOORS; f++) {
        elevator_hardware_set_button_lamp(BUTTON_CAB, f, my_elevator.elevator_state.cab_requests[f]);
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            elevator_hardware_set_button_lamp(b, f, my_elevator.worldview.hall_requests[f][b]);
    }

    pthread_mutex_unlock(&my_elevator.lock);
}

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
    (void)self;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&my_elevator.lock, &attr);
    pthread_mutexattr_destroy(&attr);

    char hw_port[16];
    snprintf(hw_port, sizeof(hw_port), "%d", ELEVATOR_HW_BASE_PORT + g_elevator_id);
    LOGI(TAG, "elevator id=%d  hardware=%s:%s  pp_port=%d",
         g_elevator_id, ELEVATOR_HARDWARE_IP, hw_port, g_process_pair_port);
    LOGD(TAG, "initializing elevator hardware");
    if (elevator_hardware_init(ELEVATOR_HARDWARE_IP, hw_port) != 0) {
        LOGE(TAG, "could not initialize elevator hardware");
        return -1;
    }

    LOGD(TAG, "elevator hardware initialized, running startup sequence");

    if (init_arg != NULL) {
        self->task_resources = malloc(sizeof(process_pair_message_t));
        if (self->task_resources == NULL) {
            LOGE(TAG, "could not allocate memmory");
            return -1;
        }
        memcpy(self->task_resources, init_arg, sizeof(process_pair_message_t));
    }

    elevator_roles_init_peer_state();

    if (elevator_net_init() != 0) {
        LOGE(TAG, "failed to initialize elevator network");
        free(self->task_resources);
        self->task_resources = NULL;
        return -1;
    }

    errno = 0;
    return 0;
}

static void elevator_cleanup(task_handle_t *self) {
    if (self->task_resources != NULL) 
        free(self->task_resources);
    elevator_net_cleanup();
    pthread_mutex_destroy(&my_elevator.lock);
}

static void *elevator_entry(task_handle_t *self) {
    int tick = 0;
    elevator_startup_sequence();
    if (self->task_resources != NULL)
        restore_state((const process_pair_message_t *)self->task_resources);

    while (g_running && self->state != TASK_STATE_STOPPING) {
        read_update_cab_state();

        if (!elevator_hardware_connected()) {
            LOGW(TAG, "hardware connection lost, reconnecting...");
            elevator_hardware_set_motor_direction(DIRN_STOP);
            while (g_running && elevator_hardware_init(NULL, NULL) != 0)
                usleep(1000 * ELEVATOR_TASK_HEARTBEAT_MS);
            if (!g_running) break;
            door_timer_ticks = 0;
            elevator_startup_sequence();
            continue;
        }

        switch (elevator_role) {
            case DISCONNECTED: elevator_logic_disconnected(); break;
            case SLAVE:        elevator_logic_slave();        break;
            case MASTER:       elevator_logic_master();       break;
            default:
                LOGE(TAG, "unhandled role: %d", elevator_role);
                elevator_role = DISCONNECTED;
                break;
        }

        elevator_local_t local = take_snapshot();
        elevator_fsm_update(&local);
        commit_snapshot(&local);

        if (++tick % 10 == 0)
            log_elevator_state(&local);

        write_elevator_state();
        usleep(1000 * ELEVATOR_TASK_HEARTBEAT_MS);
    }

    LOGD(TAG, "elevator task exiting");
    task_handle_mark_done(self);
    return NULL;
}