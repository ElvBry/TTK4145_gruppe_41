#ifndef ELEVATOR_ROLES_H
#define ELEVATOR_ROLES_H

#include <stdbool.h>
#include <rtsystem/rtsystem_config.h>
#include <rtsystem/drivers/elevator_network.h>

typedef enum {
    DISCONNECTED = 0,
    SLAVE        = 1,
    MASTER       = 2,
} elevator_role_t;

extern elevator_role_t  elevator_role;
extern net_slave_msg_t  peer_last_state[N_ELEVATORS];
extern bool             motorstop_detected[N_ELEVATORS];

void elevator_roles_init_peer_state(void);

int elevator_logic_single(void);
int elevator_logic_disconnected(void);
int elevator_logic_slave(void);
int elevator_logic_master(void);

#endif
