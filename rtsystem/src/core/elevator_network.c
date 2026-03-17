#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <rtsystem/core/elevator_network.h>
#include <rtsystem/rtsystem_config.h>

#define LOG_LEVEL LOG_LEVEL_ELEVATOR_NETWORK
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

static const char *TAG = "net";

int g_elevator_id = -1;

elevator_peer_t g_peers[N_ELEVATORS];

// Size is derived from the initializer so the list may contain more entries
// than the active N_ELEVATORS without warnings (handy when reducing N_ELEVATORS
// for testing). Keep at least N_ELEVATORS entries or indexing will be wrong.
static const char *const elevator_ips[] = ELEVATOR_NET_IP_LIST;

// Port used for the channel from elevator 'sender' to elevator 'receiver'.
// Each (sender, receiver) pair has a unique port so recv sockets are dedicated.
static int net_port(int sender, int receiver) {
    return ELEVATOR_NET_BASE_PORT + sender * N_ELEVATORS + receiver;
}

int elevator_net_init(void) {
    if (g_elevator_id < 0 || g_elevator_id >= N_ELEVATORS) {
        LOGE(TAG, "g_elevator_id not set before elevator_net_init");
        return -1;
    }

    int p = 0;
    for (int id = 0; id < N_ELEVATORS; id++) {
        if (id == g_elevator_id) continue;

        elevator_peer_t *peer = &g_peers[p];
        peer->peer_id           = id;
        peer->consecutive_losses = 0;
        peer->send_fd           = -1;
        peer->recv_fd           = -1;

        // Send socket: connected to the port peer 'id' listens on for us.
        // Using connect() lets us call send() without repeating the address.
        peer->send_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (peer->send_fd < 0) {
            LOGE_ERRNO(TAG, "socket() for send_fd (peer %d): ", id);
            goto fail;
        }
        struct sockaddr_in dst = {
            .sin_family = AF_INET,
            .sin_port   = htons(net_port(g_elevator_id, id)),
        };
        if (inet_pton(AF_INET, elevator_ips[id], &dst.sin_addr) != 1) {
            LOGE(TAG, "invalid IP for elevator %d: %s", id, elevator_ips[id]);
            goto fail;
        }
        if (connect(peer->send_fd, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            LOGE_ERRNO(TAG, "connect() send_fd (peer %d): ", id);
            goto fail;
        }
        LOGD(TAG, "peer %d send socket → %s:%d",
             id, elevator_ips[id], net_port(g_elevator_id, id));

        // Recv socket: bound to the port we expect peer 'id' to send to us on.
        // Non-blocking so recv_latest can drain without ever hanging.
        peer->recv_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (peer->recv_fd < 0) {
            LOGE_ERRNO(TAG, "socket() for recv_fd (peer %d): ", id);
            goto fail;
        }
        int opt = 1;
        setsockopt(peer->recv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        fcntl(peer->recv_fd, F_SETFL,
              fcntl(peer->recv_fd, F_GETFL, 0) | O_NONBLOCK);

        struct sockaddr_in local = {
            .sin_family      = AF_INET,
            .sin_port        = htons(net_port(id, g_elevator_id)),
            .sin_addr.s_addr = INADDR_ANY,
        };
        if (bind(peer->recv_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
            LOGE_ERRNO(TAG, "bind() recv_fd port %d (peer %d): ",
                       net_port(id, g_elevator_id), id);
            goto fail;
        }
        LOGD(TAG, "peer %d recv socket ← port %d",
             id, net_port(id, g_elevator_id));

        p++;
    }
    return 0;

fail:
    elevator_net_cleanup();
    return -1;
}

void elevator_net_cleanup(void) {
    for (int i = 0; i < N_ELEVATORS - 1; i++) {
        if (g_peers[i].send_fd >= 0) {
            close(g_peers[i].send_fd);
            g_peers[i].send_fd = -1;
        }
        if (g_peers[i].recv_fd >= 0) {
            close(g_peers[i].recv_fd);
            g_peers[i].recv_fd = -1;
        }
    }
}

int elevator_net_send(int peer_idx, const void *msg, size_t len) {
    ssize_t n = send(g_peers[peer_idx].send_fd, msg, len, 0);
    if (n != (ssize_t)len) {
        if (errno != ECONNREFUSED)
            LOGW_ERRNO(TAG, "send() to peer %d: ", g_peers[peer_idx].peer_id);
        return -1;
    }
    return 0;
}

int elevator_net_recv_raw(int peer_idx, void *buf, size_t max_len) {
    ssize_t n = recv(g_peers[peer_idx].recv_fd, buf, max_len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        LOGW_ERRNO(TAG, "recv_raw from peer %d: ", g_peers[peer_idx].peer_id);
        return -1;
    }
    return (int)n;
}

int elevator_net_recv_latest(int peer_idx, void *buf, size_t len) {
    // Drain all queued packets for this peer, keep only the last complete one.
    // Stale packets can pile up between ticks — we only care about fresh state.
    uint8_t tmp[256];
    if (len > sizeof(tmp)) {
        LOGE(TAG, "recv_latest: requested size %zu exceeds internal buffer", len);
        return -1;
    }

    int got = 0;
    while (1) {
        ssize_t n = recv(g_peers[peer_idx].recv_fd, tmp, len, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOGW_ERRNO(TAG, "recv() from peer %d: ", g_peers[peer_idx].peer_id);
            return -1;
        }
        if (n == (ssize_t)len) {
            memcpy(buf, tmp, len);
            got = 1;
        }
        // Wrong-size datagram: discard silently (corrupt or wrong sender)
    }
    return got;
}
