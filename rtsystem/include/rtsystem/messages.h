#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <rtsystem/rtsystem_config.h>
#include <rtsystem/core/elevator_hardware.h>

#define N_HALL_BUTTONS 2

// TODO: might define somewhere else after elevator setup is known.

typedef enum {
    EB_IDLE      = 0,
    EB_MOVING    = 1,
    EB_DOOR_OPEN = 2,
} elevator_behaviour_t;


// TODO: Could probably be called cab state instead, but not worth doing before having a working model
typedef struct {
    int8_t                              floor; // -1 for being in between floors/unknown
    elevator_hardware_motor_direction_t dirn;
    elevator_behaviour_t                behaviour;
    bool                                obstructed;
    bool                                cab_requests[N_FLOORS];
} elevator_state_t;

// worldview shared and updated by all elevators in network
// worldview with highest counter should be accepted
typedef struct {
    int64_t worldview_counter;
    bool    hall_requests[N_FLOORS][N_HALL_BUTTONS];
} worldview_t;

typedef enum {
    PP_MSG_HEARTBEAT,      // normal state update, backup should store and echo
    PP_MSG_SHUTDOWN,       // primary is shutting down, backup should exit cleanly
    PP_MSG_REQUEST_STATE,  // primary requests backup's last committed state (sent once on init)
} process_pair_message_type_t;

typedef struct {
    process_pair_message_type_t type;
    elevator_state_t            my_elevator_state;
    worldview_t                 worldview;
    uint32_t                    crc32;
} process_pair_message_t;

static inline uint32_t crc32(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

static inline uint32_t process_pair_message_checksum(const process_pair_message_t *m) {
    return crc32(m, offsetof(process_pair_message_t, crc32)); 
}

#endif