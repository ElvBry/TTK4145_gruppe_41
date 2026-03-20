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

static int             master_peer_idx = -1;
net_slave_msg_t peer_last_state[N_ELEVATORS];
static bool     peer_last_assignment[N_ELEVATORS][N_FLOORS][N_HALL_BUTTONS];

static int  motorstop_ticks[N_ELEVATORS]    = {0};
bool        motorstop_detected[N_ELEVATORS] = {false};
static int64_t peer_acked_counter[N_ELEVATORS] = {0};

// ----------------------------------------------------------------
// Helpers shared across roles
// ----------------------------------------------------------------

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

static void clear_detected_calls_for_known_requests(void) {
    pthread_mutex_lock(&my_elevator.lock);
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            if (my_elevator.worldview.hall_requests[f][b])
                my_elevator.detected_hall_calls[f][b] = false;
    pthread_mutex_unlock(&my_elevator.lock);
}

static void go_disconnected(void) {
    elevator_role   = DISCONNECTED;
    master_peer_idx = -1;
    pthread_mutex_lock(&my_elevator.lock);
    memset(my_elevator.assigned_halls, 0, sizeof(my_elevator.assigned_halls));
    // detected_hall_calls intentionally preserved: unconfirmed button presses
    // must survive role transitions so the new master can commit them.
    pthread_mutex_unlock(&my_elevator.lock);

    // All peer knowledge is invalid after losing the network.
    // Only worldview and own elevator state survive role transitions.
    // Fresh state arrives via handshake after reconnect.
    for (int id = 0; id < N_ELEVATORS; id++) {
        peer_last_state[id].state.floor      = -1;
        peer_last_state[id].state.dirn       = DIRN_STOP;
        peer_last_state[id].state.behaviour  = EB_IDLE;
        peer_last_state[id].state.obstructed = false;
        memset(peer_last_assignment[id], 0, sizeof(peer_last_assignment[id]));
        motorstop_ticks[id]    = 0;
        motorstop_detected[id] = false;
        peer_acked_counter[id] = 0;
    }
}

static void update_motorstop(int id, elevator_state_t *s) {
    if (s->behaviour == EB_MOVING && s->floor == -1) {
        if (++motorstop_ticks[id] >= TICKS_BEFORE_MOTORSTOP && !motorstop_detected[id]) {
            motorstop_detected[id] = true;
            LOGW(TAG, "motor stop detected on elevator (id=%d)", id);
        }
    } else {
        motorstop_ticks[id] = 0;
        motorstop_detected[id] = false;
    }
}

// ----------------------------------------------------------------
// Disconnected helpers
// ----------------------------------------------------------------

static void broadcast_own_state(void) {
    net_slave_msg_t out = {0};
    pthread_mutex_lock(&my_elevator.lock);
    out.state     = my_elevator.elevator_state;
    out.worldview = my_elevator.worldview;
    pthread_mutex_unlock(&my_elevator.lock);
    out.crc = net_slave_msg_checksum(&out);
    for (int i = 0; i < N_ELEVATORS - 1; i++)
        elevator_net_send(i, &out, sizeof(out));
}

// ----------------------------------------------------------------
// Slave helpers
// ----------------------------------------------------------------

static void send_state_to_master(void) {
    net_slave_msg_t out = {0};
    pthread_mutex_lock(&my_elevator.lock);
    out.state = my_elevator.elevator_state;
    memcpy(out.detected_hall_calls, my_elevator.detected_hall_calls,
           sizeof(out.detected_hall_calls));
    out.worldview = my_elevator.worldview;
    pthread_mutex_unlock(&my_elevator.lock);
    out.crc = net_slave_msg_checksum(&out);
    elevator_net_send(master_peer_idx, &out, sizeof(out));
}

static void apply_master_reply(net_master_msg_t *in) {
    bool worldview_updated;
    pthread_mutex_lock(&my_elevator.lock);
    memcpy(my_elevator.assigned_halls, in->assigned_halls, sizeof(my_elevator.assigned_halls));
    worldview_updated = in->worldview.worldview_counter > my_elevator.worldview.worldview_counter;
    if (worldview_updated)
        my_elevator.worldview = in->worldview;
    pthread_mutex_unlock(&my_elevator.lock);

    if (worldview_updated)
        clear_detected_calls_for_known_requests();
}

#if PARTITION_POSSIBLE
// Send our state to every non-master peer with a higher ID and check for a master reply.
// If one replies, we were in a partition and should switch to the higher-ID master immediately.
// Returns true if a new master was found and applied.
static bool probe_for_higher_master(void) {
    net_slave_msg_t probe = {0};
    pthread_mutex_lock(&my_elevator.lock);
    probe.state = my_elevator.elevator_state;
    memcpy(probe.detected_hall_calls, my_elevator.detected_hall_calls,
           sizeof(probe.detected_hall_calls));
    probe.worldview = my_elevator.worldview;
    pthread_mutex_unlock(&my_elevator.lock);
    probe.crc = net_slave_msg_checksum(&probe);

    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        if (i == master_peer_idx) continue;
        if (g_peers[i].peer_id <= g_peers[master_peer_idx].peer_id) continue;
        elevator_net_send(i, &probe, sizeof(probe));
        net_master_msg_t reply;
        if (elevator_net_recv_latest(i, &reply, sizeof(reply)) == 1 &&
            reply.crc == net_master_msg_checksum(&reply)) {
            LOGD(TAG, "peer %d (higher ID) replied as master, switching from peer %d",
                 g_peers[i].peer_id, g_peers[master_peer_idx].peer_id);
            master_peer_idx = i;
            g_peers[i].consecutive_losses = 0;
            apply_master_reply(&reply);
            return true;
        }
    }
    return false;
}
#endif

// ----------------------------------------------------------------
// Master helpers
// ----------------------------------------------------------------

// Returns true if we yielded to a higher-ID peer and should stop the tick.
// responded[i] is set true for each peer slot that sent a valid message this tick.
static bool collect_peer_states(worldview_t *proposed, bool responded[N_ELEVATORS - 1]) {
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        responded[i] = false;
        net_slave_msg_t in;
        int got = elevator_net_recv_latest(i, &in, sizeof(in));

        if (got != 1 || in.crc != net_slave_msg_checksum(&in)) {
            if (++g_peers[i].consecutive_losses >= ELEVATOR_NET_MAX_LOSSES) {
                // Peer just became lost — clear stale assignment and state so the
                // assigner never pre-seeds a call to a dead elevator next tick.
                int pid = g_peers[i].peer_id;
                memset(peer_last_assignment[pid], 0, sizeof(peer_last_assignment[pid]));
                peer_last_state[pid].state.floor      = -1;
                peer_last_state[pid].state.behaviour  = EB_IDLE;
                peer_last_state[pid].state.dirn       = DIRN_STOP;
                peer_last_state[pid].state.obstructed = false;
            }
            continue;
        }

        g_peers[i].consecutive_losses = 0;
        responded[i] = true;
        peer_last_state[g_peers[i].peer_id] = in;
        peer_acked_counter[g_peers[i].peer_id] = in.worldview.worldview_counter;

        if (g_peers[i].peer_id > g_elevator_id) {
            LOGD(TAG, "peer %d has higher ID, becoming SLAVE", g_peers[i].peer_id);
            elevator_role   = SLAVE;
            master_peer_idx = i;
            return true;
        }

        bool higher = (in.worldview.worldview_counter > proposed->worldview_counter);

#if PARTITION_POSSIBLE
        if (higher && in.worldview.master_id != proposed->master_id) {
            *proposed = in.worldview;
            for (int f = 0; f < N_FLOORS; f++)
                for (int b = 0; b < N_HALL_BUTTONS; b++)
                    proposed->hall_requests[f][b] |= in.detected_hall_calls[f][b];
        } else {
            if (higher)
                proposed->worldview_counter = in.worldview.worldview_counter;
            for (int f = 0; f < N_FLOORS; f++)
                for (int b = 0; b < N_HALL_BUTTONS; b++) {
                    proposed->hall_requests[f][b] |= in.detected_hall_calls[f][b];
                    if (in.worldview.worldview_counter >= proposed->worldview_counter)
                        proposed->hall_requests[f][b] |= in.worldview.hall_requests[f][b];
                }
        }
#else
        if (higher)
            proposed->worldview_counter = in.worldview.worldview_counter;
        for (int f = 0; f < N_FLOORS; f++)
            for (int b = 0; b < N_HALL_BUTTONS; b++) {
                proposed->hall_requests[f][b] |= in.detected_hall_calls[f][b];
                if (in.worldview.worldview_counter >= proposed->worldview_counter)
                    proposed->hall_requests[f][b] |= in.worldview.hall_requests[f][b];
            }
#endif
    }
    return false;
}

static bool all_peers_lost(void) {
    for (int i = 0; i < N_ELEVATORS - 1; i++)
        if (g_peers[i].consecutive_losses <= ELEVATOR_NET_MAX_LOSSES)
            return false;
    return true;
}

static void mark_served_requests(worldview_t *proposed, elevator_state_t own_state,
                                  bool own_assigned[N_FLOORS][N_HALL_BUTTONS],
                                  int64_t current_counter) {
    if (own_state.behaviour == EB_DOOR_OPEN && own_state.floor >= 0 && !own_state.obstructed) {
        int f = own_state.floor;
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            if (own_assigned[f][b])
                proposed->hall_requests[f][b] = false;
    }
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        if (g_peers[i].consecutive_losses > ELEVATOR_NET_MAX_LOSSES) continue;
        int pid = g_peers[i].peer_id;
        // Two-way handshake guard: only clear a request based on this peer's state
        // if the peer has acked our current worldview — i.e. it knew about the assignment.
        if (peer_acked_counter[pid] < current_counter) continue;
        elevator_state_t *ps = &peer_last_state[pid].state;
        if (ps->behaviour == EB_DOOR_OPEN && ps->floor >= 0 && !ps->obstructed) {
            int f = ps->floor;
            for (int b = 0; b < N_HALL_BUTTONS; b++)
                if (peer_last_assignment[pid][f][b])
                    proposed->hall_requests[f][b] = false;
        }
    }
}

// Commits proposed worldview if changed and snapshots hall requests as integers for the assigner.
// Returns true if the worldview was updated.
static bool commit_worldview(worldview_t proposed, int hall_out[N_FLOORS][N_HALL_BUTTONS]) {
    bool changed;
    pthread_mutex_lock(&my_elevator.lock);
    changed = memcmp(proposed.hall_requests, my_elevator.worldview.hall_requests,
                     sizeof(proposed.hall_requests)) != 0;
#if PARTITION_POSSIBLE
    bool owner_changed = (my_elevator.worldview.master_id != (int8_t)g_elevator_id);
    if (changed || owner_changed) {
        // Use max(ours, proposed) + 1 so all slaves accept the counter after a partition heal.
        int64_t base = my_elevator.worldview.worldview_counter;
        if (proposed.worldview_counter > base) base = proposed.worldview_counter;
        my_elevator.worldview.worldview_counter = base + 1;
        if (changed)
            memcpy(my_elevator.worldview.hall_requests, proposed.hall_requests,
                   sizeof(my_elevator.worldview.hall_requests));
        my_elevator.worldview.master_id = (int8_t)g_elevator_id;
    }
#else
    if (changed) {
        my_elevator.worldview.worldview_counter++;
        memcpy(my_elevator.worldview.hall_requests, proposed.hall_requests,
               sizeof(my_elevator.worldview.hall_requests));
    }
#endif
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            hall_out[f][b] = my_elevator.worldview.hall_requests[f][b] ? 1 : 0;
    pthread_mutex_unlock(&my_elevator.lock);
    return changed;
}

static void apply_new_assignments(int new_assignment[N_ELEVATORS][N_FLOORS][N_HALL_BUTTONS]) {
    pthread_mutex_lock(&my_elevator.lock);
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
}

static void build_elev_array(elevator_local_t out[N_ELEVATORS], elevator_state_t own_state) {
    memset(out, 0, sizeof(elevator_local_t) * N_ELEVATORS);
    out[g_elevator_id].state = own_state;
    pthread_mutex_lock(&my_elevator.lock);
    memcpy(out[g_elevator_id].hall_requests, my_elevator.assigned_halls,
           sizeof(out[g_elevator_id].hall_requests));
    pthread_mutex_unlock(&my_elevator.lock);
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        int pid = g_peers[i].peer_id;
        out[pid].state = peer_last_state[pid].state;
        memcpy(out[pid].hall_requests, peer_last_assignment[pid],
               sizeof(out[pid].hall_requests));
    }
}

// Two-way handshake: only reply to peers that sent us something this tick.
static void broadcast_assignments_to_peers(worldview_t wv, bool responded[N_ELEVATORS - 1]) {
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        if (!responded[i]) continue;
        int pid = g_peers[i].peer_id;
        net_master_msg_t reply = {0};
        memcpy(reply.assigned_halls, peer_last_assignment[pid], sizeof(reply.assigned_halls));
        reply.worldview = wv;
        reply.crc       = net_master_msg_checksum(&reply);
        elevator_net_send(i, &reply, sizeof(reply));
    }
}

// ----------------------------------------------------------------
// Role logic
// ----------------------------------------------------------------

int elevator_logic_single(void) {
    read_update_hall_state();
    pthread_mutex_lock(&my_elevator.lock);
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            if (my_elevator.detected_hall_calls[f][b]) {
                my_elevator.worldview.hall_requests[f][b] = true;
                my_elevator.detected_hall_calls[f][b]     = false;
            }
    pthread_mutex_unlock(&my_elevator.lock);
    return 0;
}

int elevator_logic_disconnected(void) {
    if (elevator_role != DISCONNECTED) {
        LOGE(TAG, "ERROR: inside disconnected logic without being disconnected");
        return -1;
    }

    broadcast_own_state();

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

        // Only reset losses for peers that actually responded — dead peers keep
        // their high count so the new master marks them as skip[] immediately.
        g_peers[i].consecutive_losses = 0;
        heard = true;
        if (g_peers[i].peer_id > g_elevator_id) {
            if (best_master_idx < 0 ||
                g_peers[i].peer_id > g_peers[best_master_idx].peer_id)
                best_master_idx = i;
        }
    }

    if (!heard) return 0;

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

#if PARTITION_POSSIBLE
    if (probe_for_higher_master()) return 0;
#endif

    send_state_to_master();

    net_master_msg_t in;
    int got = elevator_net_recv_latest(master_peer_idx, &in, sizeof(in));

    if (got != 1 || in.crc != net_master_msg_checksum(&in)) {
        if (++g_peers[master_peer_idx].consecutive_losses > ELEVATOR_NET_MAX_LOSSES) {
            LOGD(TAG, "master (elevator %d) lost, becoming DISCONNECTED",
                 g_peers[master_peer_idx].peer_id);
            go_disconnected();
        }
        return 0;
    }

    g_peers[master_peer_idx].consecutive_losses = 0;
    apply_master_reply(&in);
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
#if PARTITION_POSSIBLE
    proposed.master_id         = (int8_t)g_elevator_id;
#endif
    memcpy(own_assigned, my_elevator.assigned_halls, sizeof(own_assigned));
    for (int f = 0; f < N_FLOORS; f++)
        for (int b = 0; b < N_HALL_BUTTONS; b++)
            proposed.hall_requests[f][b] |= my_elevator.detected_hall_calls[f][b];
    pthread_mutex_unlock(&my_elevator.lock);

    bool responded[N_ELEVATORS - 1];
    if (collect_peer_states(&proposed, responded)) return 0;

    if (all_peers_lost() && N_ELEVATORS > 1) {
        LOGD(TAG, "all peers lost, becoming DISCONNECTED");
        go_disconnected();
        return 0;
    }

    update_motorstop(g_elevator_id, &own_state);
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        if (g_peers[i].consecutive_losses > ELEVATOR_NET_MAX_LOSSES) continue;
        update_motorstop(g_peers[i].peer_id, &peer_last_state[g_peers[i].peer_id].state);
    }

    mark_served_requests(&proposed, own_state, own_assigned, proposed.worldview_counter);

    // Two-way handshake: only commit worldview and reply if at least one slave
    // responded this tick. This ensures worldview advances only when we are
    // confirmed to be inside the network, not from unilateral observation.
    bool any_responded = false;
    for (int i = 0; i < N_ELEVATORS - 1; i++) any_responded |= responded[i];
    if (!any_responded) return 0;

    int hall_int[N_FLOORS][N_HALL_BUTTONS];
    if (commit_worldview(proposed, hall_int))
        clear_detected_calls_for_known_requests();

    elevator_local_t elev_array[N_ELEVATORS];
    build_elev_array(elev_array, own_state);

    bool skip[N_ELEVATORS];
    skip[g_elevator_id] = motorstop_detected[g_elevator_id] || own_state.obstructed;
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        int pid = g_peers[i].peer_id;
        skip[pid] = motorstop_detected[pid]
                 || peer_last_state[pid].state.obstructed
                 || (g_peers[i].consecutive_losses > ELEVATOR_NET_MAX_LOSSES);
    }

    hall_assignment_t assignment = assignHallRequests(elev_array, hall_int, skip);

    apply_new_assignments(assignment.halls);

    pthread_mutex_lock(&my_elevator.lock);
    worldview_t wv_snapshot = my_elevator.worldview;
    pthread_mutex_unlock(&my_elevator.lock);

    broadcast_assignments_to_peers(wv_snapshot, responded);

    return 0;
}

void elevator_roles_init_peer_state(void) {
    for (int i = 0; i < N_ELEVATORS; i++) {
        peer_last_state[i].state.floor = -1;
        peer_last_state[i].state.dirn = DIRN_STOP;
        peer_last_state[i].state.behaviour = EB_IDLE;
    }
}
