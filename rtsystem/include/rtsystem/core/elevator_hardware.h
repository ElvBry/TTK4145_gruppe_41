#ifndef ELEVATOR_HARDWARE_H
#define ELEVATOR_HARDWARE_H

#include <rtsystem/rtsystem_config.h>

typedef enum tag_elevator_hardware_motor_direction { 
    DIRN_DOWN = -1,
    DIRN_STOP = 0,
    DIRN_UP = 1
} elevator_hardware_motor_direction_t;

typedef enum tag_elevator_hardware_button_type { 
    BUTTON_CALL_UP = 0,
    BUTTON_CALL_DOWN = 1,
    BUTTON_CAB = 2
} elevator_hardware_button_type_t;

// ip and port set the TCP target; pass NULL to reuse values from the previous call (reconnect).
int elevator_hardware_init(const char *ip, const char *port);
int elevator_hardware_connected(void);

void elevator_hardware_set_motor_direction(elevator_hardware_motor_direction_t dirn);
void elevator_hardware_set_button_lamp(elevator_hardware_button_type_t button, int floor, int value);
void elevator_hardware_set_floor_indicator(int floor);
void elevator_hardware_set_door_open_lamp(int value);
void elevator_hardware_set_stop_lamp(int value);

int elevator_hardware_get_button_signal(elevator_hardware_button_type_t button, int floor);
int elevator_hardware_get_floor_sensor_signal(void);
int elevator_hardware_get_stop_signal(void);
int elevator_hardware_get_obstruction_signal(void);

#endif