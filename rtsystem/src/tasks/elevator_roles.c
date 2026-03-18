#include <pthread.h>
#include <string.h>

#include "rtsystem/tasks/elevator_task.h"
#include "rtsystem/tasks/elevator_roles.h"
#include "rtsystem/core/elevator_network.h"
#include "rtsystem/core/elevator_control_helper.h"
#include "rtsystem/core/elevator_hardware.h"
#include <rtsystem/rtsystem_config.h>

#define LOG_LEVEL LOG_LEVEL_BACKUP_TASK
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

static const char *TAG = "elevator_roles";

elevator_role_t elevator_role = DISCONNECTED;

static int            master_peer_idx = -1;
static net_slave_msg_t peer_last_state[N_ELEVATORS];
static bool           peer_last_assignment[N_ELEVATORS][N_FLOORS][N_HALL_BUTTONS];

static int  motorstop_ticks[N_ELEVATORS]    = {0};
static bool motorstop_detected[N_ELEVATORS] = {false};

static void read_update_hall_state(void) {
    static bool prev_hall[N_FLOORS][N_HALL_BUTTONS] = {0};
    pthread_mutex_lock(&my_elevator.lock);
    for (int f = 0; f < N_FLOORS; f++) {
        for (int b = 0; b < N_HALL_BUTTONS; b++) {
            bool signal = elevator_hardware_get_button_signal(b, f);
            if (signal && !prev_hall[f][b])
                my_elevator.detected_hall_calls[f][b] = true;
            prev_hall[f][b] = signal;
        }
    }
    pthread_mutex_unlock(&my_elevator.lock);
}

int elevator_logic_disconnected(void) {
    if (elevator_role != DISCONNECTED) {
        LOGE(TAG, "ERROR: inside disconnected logic without being disconnected");
        return -1;
    }

    net_slave_msg_t out = {0};
    pthread_mutex_lock(&my_elevator.lock);
    out.state     = my_elevator.elevator_state;
    out.worldview = my_elevator.worldview;
    pthread_mutex_unlock(&my_elevator.lock);
    out.crc = net_slave_msg_checksum(&out);
    for (int i = 0; i < N_ELEVATORS - 1; i++)
        elevator_net_send(i, &out, sizeof(out));

    int     best_master_idx = -1;
    bool    heard           = false;
    uint8_t buf[256];

    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        int n = elevator_net_recv_raw(i, buf, sizeof(buf));
        if (n <= 0) continue;

        bool valid = false;
        if (n == (int)sizeof(net_slave_msg_t)) {
            net_slave_msg_t *m = (net_slave_msg_t *)buf;
            valid = (m->crc == net_slave_msg_checksum(m));
        } else if (n == (int)sizeof(net_master_msg_t)) {
            net_master_msg_t *m = (net_master_msg_t *)buf;
            valid = (m->crc == net_master_msg_checksum(m));
        }
        if (!valid) continue;

        heard = true;
        if (g_peers[i].peer_id > g_elevator_id) {
            if (best_master_idx < 0 ||
                g_peers[i].peer_id > g_peers[best_master_idx].peer_id)
                best_master_idx = i;
        }
    }

    if (!heard) return 0;

    for (int i = 0; i < N_ELEVATORS - 1; i++)
        g_peers[i].consecutive_losses = 0;

    if (best_master_idx >= 0) {
        elevator_role   = SLAVE;
        master_peer_idx = best_master_idx;
        LOGD(TAG, "joined as SLAVE, master=elevator %d", g_peers[master_peer_idx].peer_id);
    } else {
        elevator_role = MASTER;
        LOGD(TAG, "joined as MASTER (own id=%d)", g_elevator_id);
    }
    return 0;
}

int elevator_logic_slave(void) {
    if (elevator_role != SLAVE) {
        LOGE(TAG, "ERROR: inside slave logic without being slave");
        return -1;
    }

    read_update_hall_state();

    net_slave_msg_t out = {0};
    pthread_mutex_lock(&my_elevator.lock);
    out.state = my_elevator.elevator_state;
    memcpy(out.detected_hall_calls, my_elevator.detected_hall_calls,
           sizeof(out.detected_hall_calls));
    out.worldview = my_elevator.worldview;
    pthread_mutex_unlock(&my_elevator.lock);
    out.crc = net_slave_msg_checksum(&out);
    elevator_net_send(master_peer_idx, &out, sizeof(out));

    net_master_msg_t in;
    int got = elevator_net_recv_latest(master_peer_idx, &in, sizeof(in));

    if (got != 1 || in.crc != net_master_msg_checksum(&in)) {
        if (++g_peers[master_peer_idx].consecutive_losses > ELEVATOR_NET_MAX_LOSSES) {
            LOGD(TAG, "master (elevator %d) lost, becoming DISCONNECTED",
                 g_peers[master_peer_idx].peer_id);
            elevator_role   = DISCONNECTED;
            master_peer_idx = -1;
            pthread_mutex_lock(&my_elevator.lock);
            memset(my_elevator.assigned_halls,      0, sizeof(my_elevator.assigned_halls));
            memset(my_elevator.detected_hall_calls, 0, sizeof(my_elevator.detected_hall_calls));
            pthread_mutex_unlock(&my_elevator.lock);
        }
        return 0;
    }

    g_peers[master_peer_idx].consecutive_losses = 0;

    pthread_mutex_lock(&my_elevator.lock);
    memcpy(my_elevator.assigned_halls, in.assigned_halls, sizeof(my_elevator.assigned_halls));
    if (in.worldview.worldview_counter > my_elevator.worldview.worldview_counter) {
        my_elevator.worldview = in.worldview;
        for (int f = 0; f < N_FLOORS; f++)
            for (int b = 0; b < N_HALL_BUTTONS; b++)
                if (my_elevator.worldview.hall_requests[f][b])
                    my_elevator.detected_hall_calls[f][b] = false;
    }
    pthread_mutex_unlock(&my_elevator.lock);
    return 0;
}

int elevator_logic_master(void) {
    if (elevator_role != MASTER) {
        LOGE(TAG, "ERROR: inside master logic without being master");
        return -1;
    }

    read_update_hall_state();

    bool own_assigned[N_FLOORS][N_HALL_BUTTONS];
    pthread_mutex_lock(&my_elevator.lock);
    elevator_state_t own_state = my_elevator.elevator_state;
    worldview_t proposed       = my_elevator.worldview;
    memcpy(own_assigned, my_elevator.assigned_halls, sizeof(own_assigned));
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            proposed.hall_requests[f][b] |= my_elevator.detected_hall_calls[f][b];
    pthread_mutex_unlock(&my_elevator.lock);

    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        net_slave_msg_t in;
        int got = elevator_net_recv_latest(i, &in, sizeof(in));

        if (got != 1 || in.crc != net_slave_msg_checksum(&in)) {
            g_peers[i].consecutive_losses++;
            continue;
        }

        g_peers[i].consecutive_losses = 0;
        peer_last_state[g_peers[i].peer_id] = in;

        if (g_peers[i].peer_id > g_elevator_id) {
            LOGD(TAG, "peer %d has higher ID, becoming SLAVE", g_peers[i].peer_id);
            elevator_role   = SLAVE;
            master_peer_idx = i;
            return 0;
        }

        for (int f = 0; f < N_FLOORS; f++)
            for (int b = 0; b < N_HALL_BUTTONS; b++)
                proposed.hall_requests[f][b] |= in.detected_hall_calls[f][b];
    }

    bool all_lost = true;
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        if (g_peers[i].consecutive_losses <= ELEVATOR_NET_MAX_LOSSES) {
            all_lost = false;
            break;
        }
    }
    if (all_lost && N_ELEVATORS > 1) {
        LOGD(TAG, "all peers lost, becoming DISCONNECTED");
        elevator_role = DISCONNECTED;
        pthread_mutex_lock(&my_elevator.lock);
        memset(my_elevator.assigned_halls,      0, sizeof(my_elevator.assigned_halls));
        memset(my_elevator.detected_hall_calls, 0, sizeof(my_elevator.detected_hall_calls));
        pthread_mutex_unlock(&my_elevator.lock);
        return 0;
    }

    // Motor stop detection: elevator stuck in EB_MOVING with floor == -1 for too long.
    // Counter resets whenever the elevator hits a floor sensor or stops moving.
    if (own_state.behaviour == EB_MOVING && own_state.floor == -1) {
        if (++motorstop_ticks[g_elevator_id] >= TICKS_BEFORE_MOTORSTOP && !motorstop_detected[g_elevator_id]) {
            motorstop_detected[g_elevator_id] = true;
            LOGW(TAG, "motor stop detected on own elevator (id=%d)", g_elevator_id);
        }
    } else {
        motorstop_ticks[g_elevator_id] = 0;
        motorstop_detected[g_elevator_id] = false;
    }
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        int pid = g_peers[i].peer_id;
        elevator_state_t *ps = &peer_last_state[pid].state;
        if (ps->behaviour == EB_MOVING && ps->floor == -1) {
            if (++motorstop_ticks[pid] >= TICKS_BEFORE_MOTORSTOP && !motorstop_detected[pid]) {
                motorstop_detected[pid] = true;
                LOGW(TAG, "motor stop detected on peer elevator (id=%d)", pid);
            }
        } else {
            motorstop_ticks[pid] = 0;
            motorstop_detected[pid] = false;
        }
    }

    if (own_state.behaviour == EB_DOOR_OPEN && own_state.floor >= 0) {
        int f = own_state.floor;
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            if (own_assigned[f][b])
                proposed.hall_requests[f][b] = false;
    }
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        if (g_peers[i].consecutive_losses > ELEVATOR_NET_MAX_LOSSES / 2) continue;
        int pid = g_peers[i].peer_id;
        elevator_state_t *ps = &peer_last_state[pid].state;
        if (ps->behaviour == EB_DOOR_OPEN && ps->floor >= 0) {
            int f = ps->floor;
            for (int b = 0; b < N_HALL_BUTTONS; b++)
                if (peer_last_assignment[pid][f][b])
                    proposed.hall_requests[f][b] = false;
        }
    }

    pthread_mutex_lock(&my_elevator.lock);

    if (memcmp(proposed.hall_requests, my_elevator.worldview.hall_requests,
               sizeof(proposed.hall_requests)) != 0) {
        my_elevator.worldview.worldview_counter++;
        memcpy(my_elevator.worldview.hall_requests, proposed.hall_requests,
               sizeof(my_elevator.worldview.hall_requests));
        for (int f = 0; f < N_FLOORS; f++)
            for (int b = 0; b < N_HALL_BUTTONS; b++)
                if (my_elevator.worldview.hall_requests[f][b])
                    my_elevator.detected_hall_calls[f][b] = false;
    }

    elevator_local_t elev_array[N_ELEVATORS];
    memset(elev_array, 0, sizeof(elev_array));
    elev_array[g_elevator_id].state = own_state;
    memcpy(elev_array[g_elevator_id].hall_requests, my_elevator.assigned_halls,
           sizeof(elev_array[g_elevator_id].hall_requests));
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        int pid = g_peers[i].peer_id;
        elev_array[pid].state = peer_last_state[pid].state;
        memcpy(elev_array[pid].hall_requests, peer_last_assignment[pid],
               sizeof(elev_array[pid].hall_requests));
    }

    int hall_int[N_FLOORS][N_HALL_BUTTONS];
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            hall_int[f][b] = my_elevator.worldview.hall_requests[f][b] ? 1 : 0;

    int new_assignment[N_ELEVATORS][N_FLOORS][N_HALL_BUTTONS];
    assignHallRequests(elev_array, hall_int, new_assignment);

    if (motorstop_detected[g_elevator_id])
        memset(my_elevator.assigned_halls, 0, sizeof(my_elevator.assigned_halls));
    else
        for (int f = 0; f < N_FLOORS; f++)
            for (int b = 0; b < N_HALL_BUTTONS; b++)
                my_elevator.assigned_halls[f][b] = (new_assignment[g_elevator_id][f][b] != 0);

    pthread_mutex_unlock(&my_elevator.lock);

    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        int pid = g_peers[i].peer_id;
        if (motorstop_detected[pid])
            memset(peer_last_assignment[pid], 0, sizeof(peer_last_assignment[pid]));
        else
            for (int f = 0; f < N_FLOORS; f++)
                for (int b = 0; b < N_HALL_BUTTONS; b++)
                    peer_last_assignment[pid][f][b] = (new_assignment[pid][f][b] != 0);
    }

    pthread_mutex_lock(&my_elevator.lock);
    worldview_t wv_snapshot = my_elevator.worldview;
    pthread_mutex_unlock(&my_elevator.lock);

    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        int pid = g_peers[i].peer_id;
        net_master_msg_t reply = {0};
        memcpy(reply.assigned_halls, peer_last_assignment[pid], sizeof(reply.assigned_halls));
        reply.worldview = wv_snapshot;
        reply.crc       = net_master_msg_checksum(&reply);
        elevator_net_send(i, &reply, sizeof(reply));
    }

    return 0;
}

void elevator_roles_init_peer_state(void) {
    for (int i = 0; i < N_ELEVATORS; i++)
        peer_last_state[i].state.floor = -1;
}
