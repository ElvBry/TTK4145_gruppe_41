#ifndef ELEVATOR_CONTROL_HELPER_H
#define ELEVATOR_CONTROL_HELPER_H

#include <rtsystem/rtsystem_config.h>
#include <rtsystem/messages.h>
#include <rtsystem/core/elevator_hardware.h>


typedef struct {
    elevator_state_t state;
    bool hall_requests[N_FLOORS][N_HALL_BUTTONS];
} elevator_local_t;

typedef struct {
    elevator_hardware_motor_direction_t dirn;
    elevator_behaviour_t behaviour;
} dirn_behaviour_pair_t;

dirn_behaviour_pair_t requests_chooseDirection(elevator_local_t e) __attribute__((pure));

int requests_shouldStop(elevator_local_t e) __attribute__((pure));

elevator_local_t requests_clearAtCurrentFloor(elevator_local_t e) __attribute__((pure));

typedef struct {
    int halls[N_ELEVATORS][N_FLOORS][N_HALL_BUTTONS];
} hall_assignment_t;

hall_assignment_t assignHallRequests(
    elevator_local_t elevators[N_ELEVATORS],
    int hallRequests[N_FLOORS][N_HALL_BUTTONS],
    const bool skip[N_ELEVATORS]
) __attribute__((pure));

#endif
