#include <pthread.h>
#include <string.h>

#include "rtsystem/tasks/elevator_task.h"
#include "rtsystem/tasks/elevator_fsm.h"
#include "rtsystem/tasks/elevator_roles.h"
#include "rtsystem/core/elevator_control_helper.h"
#include "rtsystem/core/elevator_hardware.h"
#include "rtsystem/core/elevator_network.h"
#include <rtsystem/rtsystem_config.h>

#define LOG_LEVEL LOG_LEVEL_BACKUP_TASK
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

static const char *TAG = "elevator_fsm";

int door_timer_ticks = 0;

elevator_local_t take_snapshot(void) {
    pthread_mutex_lock(&my_elevator.lock);
    elevator_local_t local = { .state = my_elevator.elevator_state };
    memcpy(local.hall_requests, my_elevator.assigned_halls, sizeof(local.hall_requests));
    pthread_mutex_unlock(&my_elevator.lock);
    return local;
}

void commit_snapshot(const elevator_local_t *local) {
    pthread_mutex_lock(&my_elevator.lock);
    my_elevator.elevator_state = local->state;
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

void elevator_fsm_update(elevator_local_t *e) {
    e->state.obstructed = elevator_hardware_get_obstruction_signal();

    if (e->state.behaviour == EB_IDLE) {
        if (e->state.floor == -1) {
            e->state.dirn      = DIRN_DOWN;
            e->state.behaviour = EB_MOVING;
        } else if (e->state.obstructed) {
            e->state.behaviour = EB_DOOR_OPEN;
            door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
        } else {
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
    }

    if (e->state.behaviour == EB_MOVING && e->state.floor != -1) {
        if (requests_shouldStop(*e)) {
            elevator_hardware_set_motor_direction(DIRN_STOP);
            *e = requests_clearAtCurrentFloor(*e);  // uses travel dirn before changing it
            e->state.behaviour = EB_DOOR_OPEN;
            door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
        }
    }

    if (door_timer_ticks > 0) {
        if (e->state.obstructed)
            door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
        (door_timer_ticks)--;
        if (door_timer_ticks == 0 && !e->state.obstructed) {
            *e = requests_clearAtCurrentFloor(*e);
            dirn_behaviour_pair_t pair = requests_chooseDirection(*e);
            e->state.dirn      = pair.dirn;
            e->state.behaviour = pair.behaviour;
            if (pair.behaviour == EB_DOOR_OPEN)
                door_timer_ticks = ELEVATOR_DOOR_OPEN_TICKS;
        }
    }
}

static const char *behaviour_str(elevator_behaviour_t b) {
    switch (b) {
        case EB_IDLE:      return "IDLE";
        case EB_MOVING:    return "MOVE";
        case EB_DOOR_OPEN: return "DOOR";
        default:           return "???";
    }
}

static const char *dirn_str(elevator_hardware_motor_direction_t d) {
    switch (d) {
        case DIRN_UP:   return "^";
        case DIRN_DOWN: return "v";
        case DIRN_STOP: return "-";
        default:        return "?";
    }
}

void log_elevator_state(const elevator_local_t *e) {
    static const char *role_names[] = { "DISC", "SLAV", "MSTR" };
    int peers = 0;
    for (int i = 0; i < N_ELEVATORS - 1; i++)
        if (g_peers[i].consecutive_losses <= ELEVATOR_NET_MAX_LOSSES) peers++;

    char cab[N_FLOORS + 1];
    for (int f = 0; f < N_FLOORS; f++)
        cab[f] = e->state.cab_requests[f] ? '1' : '0';
    cab[N_FLOORS] = '\0';

    char hall[N_FLOORS * N_HALL_BUTTONS + 1];
    for (int f = 0; f < N_FLOORS; f++) {
        hall[f * N_HALL_BUTTONS + 0] = e->hall_requests[f][0] ? 'U' : '-';
        hall[f * N_HALL_BUTTONS + 1] = e->hall_requests[f][1] ? 'D' : '-';
    }
    hall[N_FLOORS * N_HALL_BUTTONS] = '\0';

    LOGD(TAG, "%-4s(p=%d) f=%-2d %s/%-4s cab=%s hall=%s",
         role_names[elevator_role], peers, e->state.floor,
         dirn_str(e->state.dirn), behaviour_str(e->state.behaviour),
         cab, hall);
}
