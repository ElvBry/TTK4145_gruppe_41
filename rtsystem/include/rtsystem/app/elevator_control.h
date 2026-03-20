#ifndef ELEVATOR_CONTROL_H
#define ELEVATOR_CONTROL_H

#include <rtsystem/app/elevator_fsm.h>

extern int door_timer_heartbeats;

elevator_local_t take_snapshot(void);
void             commit_snapshot(const elevator_local_t *local);
void             elevator_control_update(elevator_local_t *e);
void             log_elevator_state(const elevator_local_t *e);

#endif
