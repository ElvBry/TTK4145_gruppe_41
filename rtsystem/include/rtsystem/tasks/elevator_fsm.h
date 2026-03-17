#ifndef ELEVATOR_FSM_H
#define ELEVATOR_FSM_H

#include <rtsystem/core/elevator_control_helper.h>

extern int door_timer_ticks;

elevator_local_t take_snapshot(void);
void             commit_snapshot(const elevator_local_t *local);
void             elevator_fsm_update(elevator_local_t *e);
void             log_elevator_state(const elevator_local_t *e);

#endif
