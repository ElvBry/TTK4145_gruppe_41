#include <netinet/in.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include "log_helper/log_helper.h"


#define DEFAULT_PORT 8080
#define DEFAULT_MAX_MSG_SIZE 65507
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_SLEEP_PERIOD_S 5

static const char *TAG = "udp_tcp_sender";

static volatile sig_atomic_t running = 1;

typedef enum { PROTO_UDP, PROTO_TCP } protocol_t;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static inline int parse_port(const char *str);
static inline int parse_size(const char *str);
static inline int parse_repetitions(const char *str);
static inline long parse_period(const char *str);

#define CHECK(result, tag, fmt, ...) \
    do { if ((result) < 0) { \
        LOGE(tag, fmt, ##__VA_ARGS__); \
        return EXIT_FAILURE; \
    } } while(0)

#define HELP_MSG() \
    LOGI(TAG, "[-h (this message)] [-T (use TCP instead of UDP)] [-p <port 1-65535>] [-m \"message\"] [-s <max_msg_size> (%d by default)] [-a <host/ip address>] [-r <repetitions -1-%d> (infinite by default)] [-t <period_s> (%d by default)]\n", DEFAULT_MAX_MSG_SIZE, INT_MAX, DEFAULT_SLEEP_PERIOD_S)


int main(int argc, char **argv) {
    int         dest_port = DEFAULT_PORT;
    const char *dest_host = DEFAULT_HOST;
    long sleep_period_s   = DEFAULT_SLEEP_PERIOD_S;
    int  repetitions      = -1; //infinite by default

    int  max_msg_size   = DEFAULT_MAX_MSG_SIZE;
    int  msg_size       = 0;
    const char *message = "";
    protocol_t protocol = PROTO_UDP;
    int  opt;
    while ((opt = getopt(argc, argv, "Tp:m:s:a:r:t:h")) != -1) {
        switch (opt) {
            case 'T':
                protocol = PROTO_TCP;
                break;
            case 'p':
                dest_port = parse_port(optarg);
                CHECK(dest_port, TAG, "Invalid port: %s (must be between 1-65535)", optarg);
                break;
            case 'm':
                message = optarg;
                break;
            case 's':
                max_msg_size = parse_size(optarg);
                CHECK(max_msg_size, TAG, "Invalid size: %s (must be between 0-65507)", optarg);
                break;
            case 'a':
                dest_host = optarg;
                break;
            case 'r':
                repetitions = parse_repetitions(optarg);
                if (repetitions != -1) CHECK(repetitions, TAG, "Invalid repetition: %s (must be between -1-%d)", optarg, INT_MAX);
                break;
            case 't':
                sleep_period_s = parse_period(optarg);
                CHECK(sleep_period_s, TAG, "Invalid period: %s (must be between 0-%ld)", optarg, LONG_MAX);
                break;
            case 'h':
                HELP_MSG();
                return EXIT_SUCCESS;
            case '?':
                LOGE(TAG, "Disallowed argument %c", optopt);
                HELP_MSG();
                return EXIT_FAILURE;
            default:
                LOGE(TAG, "DEFAULT BRANCH GETOPT: Disallowed argument %c", optopt);
                HELP_MSG();
            return EXIT_FAILURE;
        }
    }

    msg_size = strlen(message);
    if (msg_size > max_msg_size) {
        LOGW(TAG, "Message truncated from %d to %d bytes", msg_size, max_msg_size);
        msg_size = max_msg_size;
    }

    const char *proto_str = (protocol == PROTO_TCP) ? "TCP" : "UDP";

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = (protocol == PROTO_TCP) ? SOCK_STREAM : SOCK_DGRAM
    };
    struct addrinfo *res;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", dest_port);

    int err = getaddrinfo(dest_host, port_str, &hints, &res);
    if (err != 0) {
        LOGE(TAG, "getaddrinfo failed: %s", gai_strerror(err));
        return EXIT_FAILURE;
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        LOGE_ERRNO(TAG, "Failed to create socket");
        freeaddrinfo(res);
        return EXIT_FAILURE;
    }
    LOGD(TAG, "[%s] Created socket", proto_str);

    // TCP requires connection, UDP uses sendto()
    if (protocol == PROTO_TCP) {
        if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
            LOGE_ERRNO(TAG, "connect() failed");
            close(sockfd);
            freeaddrinfo(res);
            return EXIT_FAILURE;
        }
        LOGD(TAG, "[%s] Connected to %s:%d", proto_str, dest_host, dest_port);
    }

    struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = 0 // Allow for interrupt
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    LOGI(TAG, "[%s] Sending to %s:%d", proto_str, dest_host, dest_port);
    while (running) {
        ssize_t bytes_sent;
        if (protocol == PROTO_TCP) {
            bytes_sent = send(sockfd, message, msg_size, 0);
        } else {
            bytes_sent = sendto(sockfd, message, msg_size, 0,
                                res->ai_addr, res->ai_addrlen);
        }
        if (bytes_sent < 0) {
            if (errno == EINTR) continue;
            LOGE_ERRNO(TAG, "send() failed");
            close(sockfd);
            freeaddrinfo(res);
            return EXIT_FAILURE;
        }
        LOGI(TAG, "[%s] Sent %zd bytes -- Message: %.*s",
            proto_str, bytes_sent, msg_size, message);

        if (repetitions > 0)  repetitions--;
        if (repetitions == 0) break;

        sleep(sleep_period_s);
    }

    if (!running) {
        LOGD(TAG, "Received shutdown signal, exiting gracefully");
    }
    close(sockfd);
    freeaddrinfo(res);
    return EXIT_SUCCESS;
}

static inline int parse_port(const char *str) {
    char *endptr;
    errno = 0;
    const long port = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    if (port > 65535 || port < 1) {
        return -1;
    }

    return (int)port;
}

static inline int parse_size(const char *str) {
    char *endptr;
    errno = 0;
    const long size = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    if (size > 65507 || size < 0) {
        return -1;
    }

    return (int)size;
}

static inline int parse_repetitions(const char *str) {
    char *endptr;
    errno = 0;
    const long repetitions = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -2;
    }

    if (repetitions < -1 || repetitions > INT_MAX) {
        return -2;
    }

    return (int)repetitions;
}

static inline long parse_period(const char *str) {
    char *endptr;
    errno = 0;
    const long period = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    if (period < 0) {
        return -1;
    }

    return period;
}
