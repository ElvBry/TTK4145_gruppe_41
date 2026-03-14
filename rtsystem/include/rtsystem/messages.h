#ifndef MESSAGES_H
#define MESSAGES_H

#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>

#include <rtsystem/rtsystem_config.h>

// TODO: might define somewhere else after elevator setup is known.
// might also be named cab state or something similar
// data sent from slave to master each heartbeat
typedef struct {
    // behaviour {idle, moving, doorOpen}
    // floor 
    // direction {down, stop, up}
    // cabRequests {bool [N_floors]}
    uint8_t payload_placeholder;
} elevator_state_t;

// worldview shared and updated by all elevators in network
typedef struct {
    int64_t worldview_counter;
    bool    hall_requests[N_FLOORS][2];
} worldview_t;

typedef enum {
    PP_MSG_HEARTBEAT, // normal state update, backup should store and echo
    PP_MSG_SHUTDOWN,  // primary is shutting down, backup should exit cleanly
} process_pair_message_type_t;

// message used by process_pairs.
// only information required about system after restart,
// is stored safely by backup through two sided commit process with primary.
typedef struct {
    process_pair_message_type_t type;
    elevator_state_t            my_elevator_state;
    worldview_t                 worldview;
    uint32_t                    crc32;
} process_pair_message_t;
// TODO: add size check using static_assert() later in case N_floors changes mess stuff up

// checksum function for creating and checking integrity of data
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