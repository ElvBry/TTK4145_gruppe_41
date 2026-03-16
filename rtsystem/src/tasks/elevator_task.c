#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "rtsystem/tasks/elevator_task.h"
#include "rtsystem/core/elevator_control_helper.h"
#include "rtsystem/core/elevator_hardware.h"
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

local_elevator_t my_elevator = {0};

int door_timer_ticks = 0;

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

int elevator_startup_sequence() {
    int8_t current_floor = -1;

    while (current_floor == -1) {
        pthread_mutex_lock(&my_elevator.lock);
        my_elevator.elevator_state.behaviour = EB_MOVING;
        pthread_mutex_unlock(&my_elevator.lock);
        elevator_hardware_set_motor_direction(DIRN_DOWN);
        usleep(1000 * ELEVATOR_TASK_HEARTBEAT_MS);
        current_floor = elevator_hardware_get_floor_sensor_signal();
    }
    pthread_mutex_lock(&my_elevator.lock);
    my_elevator.elevator_state.floor     = current_floor;
    my_elevator.elevator_state.dirn      = DIRN_STOP;
    my_elevator.elevator_state.behaviour = EB_IDLE;
    elevator_hardware_set_motor_direction(DIRN_STOP);
    pthread_mutex_unlock(&my_elevator.lock);
    LOGD(TAG, "startup sequence done, at floor: %d", current_floor);
    return 0;
}

void read_update_cab_state() {
    pthread_mutex_lock(&my_elevator.lock);
    for (int f = 0; f < N_FLOORS; f++)
        my_elevator.elevator_state.cab_requests[f] |= elevator_hardware_get_button_signal(BUTTON_CAB, f);
    my_elevator.elevator_state.floor = elevator_hardware_get_floor_sensor_signal();
    pthread_mutex_unlock(&my_elevator.lock);
}

void read_update_hall_state() {
    static bool prev_hall[N_FLOORS][N_HALL_BUTTONS] = {0};
    pthread_mutex_lock(&my_elevator.lock);
    for (int f = 0; f < N_FLOORS; f++) {
        for (int b = 0; b < N_HALL_BUTTONS; b++) {
            bool signal = elevator_hardware_get_button_signal(b, f);
            if (signal && !prev_hall[f][b])
                my_elevator.detected_hall_calls[f][b] = true;
            prev_hall[f][b] = signal;
        }
    }
    pthread_mutex_unlock(&my_elevator.lock);
}

void write_elevator_state() {
    pthread_mutex_lock(&my_elevator.lock);

    elevator_hardware_motor_direction_t safe_dirn = my_elevator.elevator_state.dirn;
    if (safe_dirn == DIRN_DOWN && my_elevator.elevator_state.floor == 0)
        safe_dirn = DIRN_STOP;
    if (safe_dirn == DIRN_UP && my_elevator.elevator_state.floor == N_FLOORS - 1)
        safe_dirn = DIRN_STOP;
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

static void elevator_fsm_update(elevator_local_t *e) {
    // Idle: check if there are pending requests
    if (e->state.behaviour == EB_IDLE) {
        dirn_behaviour_pair_t pair = requests_chooseDirection(*e);
        if (pair.behaviour != EB_IDLE) {
            e->state.dirn      = pair.dirn;
            e->state.behaviour = pair.behaviour;
            if (pair.behaviour == EB_DOOR_OPEN) {
                *e = requests_clearAtCurrentFloor(*e);
                door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
            }
        }
    }

    // Moving: check if we should stop
    if (e->state.behaviour == EB_MOVING && e->state.floor != -1) {
        if (requests_shouldStop(*e)) {
            e->state.dirn = DIRN_STOP;
            elevator_hardware_set_motor_direction(DIRN_STOP);
            *e = requests_clearAtCurrentFloor(*e);
            e->state.behaviour = EB_DOOR_OPEN;
            door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
        }
    }

    // Door timer. Paused while obstructed
    if (door_timer_ticks > 0) {
        if (elevator_hardware_get_obstruction_signal())
            door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
        (door_timer_ticks)--;
        if (door_timer_ticks == 0) {
            *e = requests_clearAtCurrentFloor(*e);
            dirn_behaviour_pair_t pair = requests_chooseDirection(*e);
            e->state.dirn      = pair.dirn;
            e->state.behaviour = pair.behaviour;
            if (pair.behaviour == EB_DOOR_OPEN)
                door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
        }
    }
}


static elevator_local_t take_snapshot() {
    pthread_mutex_lock(&my_elevator.lock);
    elevator_local_t local = { .state = my_elevator.elevator_state };
    memcpy(local.hall_requests, my_elevator.assigned_halls, sizeof(local.hall_requests));
    pthread_mutex_unlock(&my_elevator.lock);
    return local;
}

static void commit_snapshot(const elevator_local_t *local) {
    pthread_mutex_lock(&my_elevator.lock);
    my_elevator.elevator_state = local->state;
    // Only write back hall requests if there is a single elevator, master manages hall requests in connected system
    if (N_ELEVATORS == 1) {                                                                                                                   
        for (int f = 0; f < N_FLOORS; f++)
            for (int b = 0; b < N_HALL_BUTTONS; b++)                                                                                          
                my_elevator.worldview.hall_requests[f][b] |= my_elevator.detected_hall_calls[f][b];                                         
        my_elevator.worldview.worldview_counter++;
        memset(my_elevator.detected_hall_calls, 0, sizeof(my_elevator.detected_hall_calls));
        memcpy(my_elevator.assigned_halls, local->hall_requests, sizeof(my_elevator.assigned_halls));
    }
    pthread_mutex_unlock(&my_elevator.lock);
}

static const char *behaviour_str(elevator_behaviour_t b) {
    switch (b) {
        case EB_IDLE:      return "IDLE";
        case EB_MOVING:    return "MOVING";
        case EB_DOOR_OPEN: return "DOOR_OPEN";
        default:           return "UNKNOWN";
    }
}

static const char *dirn_str(elevator_hardware_motor_direction_t d) {
    switch (d) {
        case DIRN_UP:   return "UP";
        case DIRN_DOWN: return "DOWN";
        case DIRN_STOP: return "STOP";
        default:        return "UNKNOWN";
    }
}

static void log_elevator_state(const elevator_local_t *e) {
    char cab[N_FLOORS + 1];
    for (int f = 0; f < N_FLOORS; f++)
        cab[f] = e->state.cab_requests[f] ? '1' : '0';
    cab[N_FLOORS] = '\0';

    char hall[N_FLOORS * 3 + 1];
    int pos = 0;
    for (int f = 0; f < N_FLOORS; f++) {
        hall[pos++] = e->hall_requests[f][0] ? 'U' : '-';
        hall[pos++] = e->hall_requests[f][1] ? 'D' : '-';
        if (f < N_FLOORS - 1) hall[pos++] = ' ';
    }
    hall[pos] = '\0';

    LOGD(TAG, "state: floor=%-2d dirn=%-4s behaviour=%-9s | cab=[%s] hall=[%s]",
         e->state.floor, dirn_str(e->state.dirn), behaviour_str(e->state.behaviour),
         cab, hall);
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

    LOGD(TAG, "initializing elevator hardware");
    if (elevator_hardware_init() != 0) {
        LOGE(TAG, "could not initialize elevator hardware");
        return -1;
    }

    LOGD(TAG, "elevator hardware initialized, running startup sequence");
    elevator_startup_sequence();

    if (init_arg != NULL)
        restore_state((const process_pair_message_t *)init_arg);

    errno = 0;
    return 0;
}

static void elevator_cleanup(task_handle_t *self) {
    (void)self;
    // TODO: might cause concurrency issues with primary, fix when implemented
    pthread_mutex_destroy(&my_elevator.lock);
}

static void *elevator_entry(task_handle_t *self) {
    int tick = 0;

    while (g_running && self->state != TASK_STATE_STOPPING) {
        read_update_cab_state();
        read_update_hall_state();   // single elevator: uses updated state from hardware
                                    // multi elevator: replace with receive_hall_assignments()

        elevator_local_t local = take_snapshot();
        elevator_fsm_update(&local);
        commit_snapshot(&local);    // TODO: also propose state to primary for process pair commit

        if (++tick % 10 == 0)
            log_elevator_state(&local);

        write_elevator_state();
        usleep(1000 * ELEVATOR_TASK_HEARTBEAT_MS);
    }

    LOGD(TAG, "elevator task exiting");
    task_handle_mark_done(self);
    return NULL;
}