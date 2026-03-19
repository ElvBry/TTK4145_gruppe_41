#include <rtsystem/core/elevator_control_helper.h>
#include <rtsystem/rtsystem_config.h>
#include "rtsystem/core/elevator_hardware.h"
#include <limits.h>
#include <string.h>
#include <limits.h>


// ============================================================
// Section 1: requests
// Ported from Project_resources/elev_algo/requests.c.
//
// Type mapping from elev_algo -> this file:
//   Elevator              -> elevator_local_t
//   e.floor               -> e.state.floor
//   e.dirn                -> e.state.dirn
//   e.behaviour           -> e.state.behaviour
//   e.requests[f][B_HallUp]   -> e.hall_requests[f][0]
//   e.requests[f][B_HallDown] -> e.hall_requests[f][1]
//   e.requests[f][B_Cab]      -> e.state.cab_requests[f]
//   EB_Idle      -> EB_IDLE
//   EB_DoorOpen  -> EB_DOOR_OPEN
//   EB_Moving    -> EB_MOVING
//   D_Up/D_Down/D_Stop -> DIRN_UP/DIRN_DOWN/DIRN_STOP
//   DirnBehaviourPair  -> dirn_behaviour_pair_t
// ============================================================

// Returns 1 if any request (hall or cab) exists above the current floor.
static int requests_above(elevator_local_t e) {
    for (int f = e.state.floor + 1; f < N_FLOORS; f++) {
        if (e.hall_requests[f][0] || e.hall_requests[f][1] || e.state.cab_requests[f])
            return 1;
    }
    return 0;
}

// Returns 1 if any request (hall or cab) exists below the current floor.
static int requests_below(elevator_local_t e) {
    for (int f = 0; f < e.state.floor; f++) {
        if (e.hall_requests[f][0] || e.hall_requests[f][1] || e.state.cab_requests[f])
            return 1;
    }
    return 0;
}

// Returns 1 if any request (hall or cab) exists at the current floor.
static int requests_here(elevator_local_t e) {
    int f = e.state.floor;
    return e.hall_requests[f][0] || e.hall_requests[f][1] || e.state.cab_requests[f];
}

// Determine the next direction and behaviour based on all pending requests.
// Logic: continue in current direction if requests exist there; otherwise
// turn around, service current floor, or go idle.
dirn_behaviour_pair_t requests_chooseDirection(elevator_local_t e) {
    switch (e.state.dirn) {
    case DIRN_UP:
        return  requests_above(e) ? (dirn_behaviour_pair_t){DIRN_UP,   EB_MOVING}    :
                requests_here(e)  ? (dirn_behaviour_pair_t){DIRN_DOWN, EB_DOOR_OPEN} :
                requests_below(e) ? (dirn_behaviour_pair_t){DIRN_DOWN, EB_MOVING}    :
                                    (dirn_behaviour_pair_t){DIRN_STOP, EB_IDLE}      ;
    case DIRN_DOWN:
        return  requests_below(e) ? (dirn_behaviour_pair_t){DIRN_DOWN, EB_MOVING}    :
                requests_here(e)  ? (dirn_behaviour_pair_t){DIRN_UP,   EB_DOOR_OPEN} :
                requests_above(e) ? (dirn_behaviour_pair_t){DIRN_UP,   EB_MOVING}    :
                                    (dirn_behaviour_pair_t){DIRN_STOP, EB_IDLE}      ;
    case DIRN_STOP:
    default:
        // When stopped: service current floor first, then pick a direction.
        return  requests_here(e)  ? (dirn_behaviour_pair_t){DIRN_STOP, EB_DOOR_OPEN} :
                requests_above(e) ? (dirn_behaviour_pair_t){DIRN_UP,   EB_MOVING}    :
                requests_below(e) ? (dirn_behaviour_pair_t){DIRN_DOWN, EB_MOVING}    :
                                    (dirn_behaviour_pair_t){DIRN_STOP, EB_IDLE}      ;
    }
}

// Returns 1 if the elevator should stop at its current floor.
// Stops when: there is a request in the travel direction, a cab request,
// or no further requests exist in the current direction.
int requests_shouldStop(elevator_local_t e) {
    int f = e.state.floor;
    switch (e.state.dirn) {
    case DIRN_DOWN:
        return e.hall_requests[f][1] || e.state.cab_requests[f] || !requests_below(e);
    case DIRN_UP:
        return e.hall_requests[f][0] || e.state.cab_requests[f] || !requests_above(e);
    case DIRN_STOP:
    default:
        return 1;
    }
}

// Clear applicable requests at the current floor. Returns the modified elevator.
// - Always clears the cab request at this floor.
// - Clears only the hall request matching the announced travel direction.
// - DIRN_STOP (idle, no announced direction) clears both hall requests.
// - The opposite direction's call is intentionally left: if no further requests
//   exist in the announced direction, requests_chooseDirection will return
//   EB_DOOR_OPEN in the new direction, giving the door-change announcement.
elevator_local_t requests_clearAtCurrentFloor(elevator_local_t e) {
    int f = e.state.floor;
    e.state.cab_requests[f] = 0;

    switch (e.state.dirn) {
    case DIRN_UP:
        e.hall_requests[f][0] = 0;
        break;

    case DIRN_DOWN:
        e.hall_requests[f][1] = 0;
        break;

    case DIRN_STOP:
    default:
        e.hall_requests[f][0] = 0;
        e.hall_requests[f][1] = 0;
        break;
    }

    return e;
}


// ============================================================
// Section 2: hall_request_assigner
// Ported from Project-resources/cost_fns/hall_request_assigner/
//
// Additional type mappings:
//   SimState.e  Elevator -> elevator_local_t
//   e.requests[f][B_HallUp/Down] -> e.hall_requests[f][0/1]
//   e.requests[f][B_Cab]         -> e.state.cab_requests[f]
//   e.floor / e.dirn / e.behaviour -> e.state.floor / .dirn / .behaviour
// ============================================================

// Cost constants (arbitrary units; only relative values matter)
#define TRAVEL_TIME    2
#define DOOR_OPEN_TIME 3
#define SIM_TIME_BLOCKED (INT_MAX / 4)

// Tracks one hall call: whether active and which elevator is assigned to it.
typedef struct {
    int active;
    int assignedTo;  // -1 = unassigned, 0..N_ELEVATORS-1 = assigned
} SimReq;

// Simulation state for one elevator: a copy of the elevator + accumulated cost.
typedef struct {
    elevator_local_t e;
    int              time;
} SimState;

static int state_has_known_floor(const elevator_local_t *e) {
    return e->state.floor >= 0 && e->state.floor < N_FLOORS;
}

static int state_is_active(const elevator_local_t *e) {
    switch (e->state.behaviour) {
    case EB_MOVING:
        return e->state.dirn != DIRN_STOP &&
               e->state.floor >= -1 &&
               e->state.floor < N_FLOORS;
    case EB_IDLE:
    case EB_DOOR_OPEN:
        return state_has_known_floor(e);
    default:
        return 0;
    }
}

// Overwrite the hall slots in the elevator with the currently unassigned calls.
// Called before every decision step so the elevator "sees" all available calls.
static void injectUnassignedHalls(elevator_local_t *e, SimReq reqs[N_FLOORS][N_HALL_BUTTONS]) {
    for (int f = 0; f < N_FLOORS; f++) {
        e->hall_requests[f][0] = (reqs[f][0].active && reqs[f][0].assignedTo < 0) ? 1 : 0;
        e->hall_requests[f][1] = (reqs[f][1].active && reqs[f][1].assignedTo < 0) ? 1 : 0;
    }
}

// Clear requests at the current floor and record any hall calls taken as assigned.
static void sim_clearAndAssign(SimState *s, int elevIdx, SimReq reqs[N_FLOORS][N_HALL_BUTTONS]) {
    int f       = s->e.state.floor;
    int hadUp   = s->e.hall_requests[f][0];
    int hadDown = s->e.hall_requests[f][1];

    s->e = requests_clearAtCurrentFloor(s->e);

    if (hadUp   && !s->e.hall_requests[f][0] && reqs[f][0].active && reqs[f][0].assignedTo < 0)
        reqs[f][0].assignedTo = elevIdx;
    if (hadDown && !s->e.hall_requests[f][1] && reqs[f][1].active && reqs[f][1].assignedTo < 0)
        reqs[f][1].assignedTo = elevIdx;
}

// Returns 1 if any hall call is still unassigned.
static int anyUnassigned(SimReq reqs[N_FLOORS][N_HALL_BUTTONS]) {
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            if (reqs[f][b].active && reqs[f][b].assignedTo < 0)
                return 1;
    return 0;
}

// Returns 1 if the elevator has any cab requests pending.
static int hasCabRequests(elevator_local_t *e) {
    for (int f = 0; f < N_FLOORS; f++)
        if (e->state.cab_requests[f])
            return 1;
    return 0;
}

// Edge-case check: safe to assign immediately when no elevator has cab requests
// and every unassigned call has an elevator already sitting at that floor.
static int immediatelyAssignable(SimReq reqs[N_FLOORS][N_HALL_BUTTONS], SimState states[N_ELEVATORS]) {
    for (int i = 0; i < N_ELEVATORS; i++)
        if (hasCabRequests(&states[i].e))
            return 0;

    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            if (reqs[f][b].active && reqs[f][b].assignedTo < 0) {
                int found = 0;
                for (int i = 0; i < N_ELEVATORS; i++)
                    if (states[i].e.state.floor == f)
                        found = 1;
                if (!found)
                    return 0;
            }
    return 1;
}

// Assign remaining unassigned calls directly to an elevator at the same floor.
static void assignImmediate(SimReq reqs[N_FLOORS][N_HALL_BUTTONS], SimState states[N_ELEVATORS]) {
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            if (reqs[f][b].active && reqs[f][b].assignedTo < 0)
                for (int i = 0; i < N_ELEVATORS; i++)
                    if (states[i].e.state.floor == f) {
                        reqs[f][b].assignedTo = i;
                        break;
                    }
}

// Returns the index of the elevator with the lowest accumulated time cost.
static int lowestTimeIdx(SimState states[N_ELEVATORS]) {
    int minIdx = -1;
    for (int i = 0; i < N_ELEVATORS; i++) {
        if (!state_is_active(&states[i].e))
            continue;
        if (minIdx < 0 || states[i].time < states[minIdx].time)
            minIdx = i;
    }
    return minIdx;
}

// Put each elevator into a well-defined starting state before the main loop.
// - Idle/DoorOpen: immediately take any hall calls at the current floor.
// - Moving:        advance one floor at half travel cost (position within segment unknown).
static void performInitialMove(SimState *s, int elevIdx, SimReq reqs[N_FLOORS][N_HALL_BUTTONS]) {
    if (s->time >= SIM_TIME_BLOCKED || !state_is_active(&s->e)) {
        s->time = SIM_TIME_BLOCKED;
        return;
    }

    injectUnassignedHalls(&s->e, reqs);

    switch (s->e.state.behaviour) {
    case EB_DOOR_OPEN:
        s->time += DOOR_OPEN_TIME / 2;
        /* fall through */
    case EB_IDLE: {
        int f = s->e.state.floor;
        for (int b = 0; b < N_HALL_BUTTONS; b++) {
            if (reqs[f][b].active && reqs[f][b].assignedTo < 0) {
                reqs[f][b].assignedTo = elevIdx;
                s->time += DOOR_OPEN_TIME;
            }
        }
        break;
    }

    case EB_MOVING:
        // When the car is already on a floor sensor and would stop there in the
        // next control step, simulate the stop instead of skipping past the floor.
        if (state_has_known_floor(&s->e) && requests_shouldStop(s->e)) {
            s->time += DOOR_OPEN_TIME;
            sim_clearAndAssign(s, elevIdx, reqs);
            s->e.state.behaviour = EB_DOOR_OPEN;
        } else {
            s->e.state.floor += s->e.state.dirn;
            s->time          += TRAVEL_TIME / 2;
        }
        break;
    }
}

// Advance one elevator one step: either stop and open doors, or move one floor.
static void performSingleMove(SimState *s, int elevIdx, SimReq reqs[N_FLOORS][N_HALL_BUTTONS]) {
    if (!state_is_active(&s->e)) {
        s->time = SIM_TIME_BLOCKED;
        return;
    }

    injectUnassignedHalls(&s->e, reqs);

    switch (s->e.state.behaviour) {

    case EB_MOVING:
        if (state_has_known_floor(&s->e) && requests_shouldStop(s->e)) {
            s->time += DOOR_OPEN_TIME;
            sim_clearAndAssign(s, elevIdx, reqs);
            s->e.state.behaviour = EB_DOOR_OPEN;
        } else {
            s->e.state.floor += s->e.state.dirn;
            s->time          += TRAVEL_TIME;
        }
        break;

    case EB_IDLE:
    case EB_DOOR_OPEN: {
        if (!state_has_known_floor(&s->e)) {
            s->time = SIM_TIME_BLOCKED;
            break;
        }
        dirn_behaviour_pair_t pair = requests_chooseDirection(s->e);
        s->e.state.dirn = pair.dirn;

        if (pair.dirn == DIRN_STOP) {
            // No direction chosen: check if there is a request right here
            int reqHere = s->e.hall_requests[s->e.state.floor][0] ||
                          s->e.hall_requests[s->e.state.floor][1] ||
                          s->e.state.cab_requests[s->e.state.floor];
            if (reqHere) {
                sim_clearAndAssign(s, elevIdx, reqs);
                s->time += DOOR_OPEN_TIME;
                s->e.state.behaviour = EB_DOOR_OPEN;
            } else {
                s->e.state.behaviour = EB_IDLE;
            }
        } else {
            s->e.state.behaviour  = EB_MOVING;
            s->e.state.floor     += s->e.state.dirn;
            s->time              += TRAVEL_TIME;
        }
        break;
    }
    }
}

// Assign active hall calls to elevators based on simulated travel cost.
void assignHallRequests(elevator_local_t elevators[N_ELEVATORS],
                        int              hallRequests[N_FLOORS][N_HALL_BUTTONS],
                        int              output[N_ELEVATORS][N_FLOORS][N_HALL_BUTTONS],
                        const bool       skip[N_ELEVATORS]) {
    // Build request tracking table. Pre-seed existing assignments so the
    // optimizer only runs for truly new requests. This prevents an already-
    // assigned elevator from losing its request due to a tick-by-tick cost
    // fluctuation (e.g. while the elevator is between floor sensors).
    // A request is re-optimized if its elevator is in the skip set.
    SimReq reqs[N_FLOORS][N_HALL_BUTTONS];
    for (int f = 0; f < N_FLOORS; f++) {
        for (int b = 0; b < N_HALL_BUTTONS; b++) {
            int pre_assigned = -1;
            for (int i = 0; i < N_ELEVATORS; i++) {
                if (!skip[i] && elevators[i].hall_requests[f][b]) {
                    pre_assigned = i;
                    break;
                }
            }
            reqs[f][b] = (SimReq){ hallRequests[f][b], pre_assigned };
        }
    }

    // Copy elevator states into simulation. Hall slots are zeroed here and
    // injected fresh each step.
    SimState states[N_ELEVATORS];
    for (int i = 0; i < N_ELEVATORS; i++) {
        states[i].e    = elevators[i];
        states[i].time = skip[i] ? INT_MAX / 2 : 0;
        for (int f = 0; f < N_FLOORS; f++) {
            states[i].e.hall_requests[f][0] = 0;
            states[i].e.hall_requests[f][1] = 0;
        }
    }

    // Give each elevator an initial move to put them in a comparable state.
    for (int i = 0; i < N_ELEVATORS; i++)
        performInitialMove(&states[i], i, reqs);

    // Main loop: always advance the cheapest elevator until all calls are assigned.
    while (anyUnassigned(reqs)) {
        if (immediatelyAssignable(reqs, states)) {
            assignImmediate(reqs, states);
            break;
        }
        int i = lowestTimeIdx(states);
        if (i < 0)
            break;
        performSingleMove(&states[i], i, reqs);
    }

    // Write results to output array.
    memset(output, 0, sizeof(int) * N_ELEVATORS * N_FLOORS * N_HALL_BUTTONS);
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            if (reqs[f][b].active && reqs[f][b].assignedTo >= 0)
                output[reqs[f][b].assignedTo][f][b] = 1;
}
