#ifndef ELEVATOR_NETWORK_H
#define ELEVATOR_NETWORK_H

#include <stdint.h>
#include <stddef.h>
#include <rtsystem/rtsystem_config.h>
#include <rtsystem/messages.h>

extern int g_elevator_id;

typedef struct {
    elevator_state_t state;
    bool             detected_hall_calls[N_FLOORS][N_HALL_BUTTONS];
    worldview_t      worldview;
    uint32_t         crc;
} net_slave_msg_t;

typedef struct {
    bool        assigned_halls[N_FLOORS][N_HALL_BUTTONS];
    worldview_t worldview;
    uint32_t    crc;
} net_master_msg_t;

static inline uint32_t net_slave_msg_checksum(const net_slave_msg_t *m) {
    return crc32(m, offsetof(net_slave_msg_t, crc));
}

static inline uint32_t net_master_msg_checksum(const net_master_msg_t *m) {
    return crc32(m, offsetof(net_master_msg_t, crc));
}

typedef struct {
    int  peer_id;
    int  send_fd;
    int  recv_fd;
    int  consecutive_losses;
} elevator_peer_t;

// Active peers, indexed 0 .. N_ELEVATORS-2 (own ID is skipped).
extern elevator_peer_t g_peers[N_ELEVATORS];

// Open all peer sockets. g_elevator_id must be set before calling.
// Returns 0 on success, -1 on failure.
int elevator_net_init(void);

// Close all peer sockets.
void elevator_net_cleanup(void);



// Send msg of len bytes to the peer at index peer_idx.
// Returns 0 on success, -1 on error.
int elevator_net_send(int peer_idx, const void *msg, size_t len);

// Drain all pending packets for peer peer_idx, keeping only the last full message.
// Writes it to buf (caller must pass the correct expected len).
// Returns  1 if a fresh message was received this call,
//          0 if the queue was empty (no new data),
//         -1 on a socket error.
int elevator_net_recv_latest(int peer_idx, void *buf, size_t len);

// Receive one raw datagram from peer peer_idx (up to max_len bytes).
// Does not filter by expected size — used when message type is unknown.
// Returns actual bytes received, 0 if nothing waiting, -1 on socket error.
int elevator_net_recv_raw(int peer_idx, void *buf, size_t max_len);

#endif
