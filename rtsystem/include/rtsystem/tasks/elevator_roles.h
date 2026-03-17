#ifndef ELEVATOR_ROLES_H
#define ELEVATOR_ROLES_H

typedef enum {
    DISCONNECTED = 0,
    SLAVE        = 1,
    MASTER       = 2,
} elevator_role_t;

extern elevator_role_t elevator_role;

void elevator_roles_init_peer_state(void);

int elevator_logic_disconnected(void);
int elevator_logic_slave(void);
int elevator_logic_master(void);

#endif
