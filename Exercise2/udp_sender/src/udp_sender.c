#include <netinet/in.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include "log_helper/log_helper.h"


#define DEFAULT_PORT 8080
#define DEFAULT_MAX_MSG_SIZE 65507
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_SLEEP_PERIOD_S 5

static const char *TAG = "udp_sender";

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static inline int parse_port(const char *str);
static inline int parse_size(const char *str);
static inline int parse_repetitions(const char *str);
static inline int parse_ip(const char *str, char *dest, size_t dest_size);
static inline long parse_period(const char *str);

#define CHECK(result, tag, fmt, ...) \
    do { if ((result) < 0) { \
        LOGE(tag, fmt, ##__VA_ARGS__); \
        return EXIT_FAILURE; \
    } } while(0)

#define HELP_MSG() \
    LOGI(TAG, "[-h (this message)] [-p <port 1-65535>] [-m \"message\"] [-s <max_msg_size> (%d by default)] [-a <ip address>] [-r <repetitions -1-%d> (infinite by default)] [-t <period_s> (%d by default)]\n", DEFAULT_MAX_MSG_SIZE, INT_MAX, DEFAULT_SLEEP_PERIOD_S)


int main(int argc, char **argv) {
    int  dest_port      = DEFAULT_PORT;
    char dest_ip[16]    = DEFAULT_IP;
    long sleep_period_s = DEFAULT_SLEEP_PERIOD_S;
    int  repetitions    = -1; //infinite by default
    int  max_msg_size   = DEFAULT_MAX_MSG_SIZE;
    const char *message = "";
    int  msg_size       = 0;
    int  opt;
    while ((opt = getopt(argc, argv, "p:m:s:a:r:t:h")) != -1) {
        switch (opt) {
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
                CHECK(parse_ip(optarg, dest_ip, sizeof(dest_ip)), TAG, "Invalid IP: %s", optarg);
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

    int  udp_tx_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_tx_socket < 0) {
        LOGE_ERRNO(TAG, "failed to create socket");
        return EXIT_FAILURE;
    }
    LOGD(TAG, "Created tx socket");

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(dest_port)
    };
    inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);


    LOGD(TAG, "Sending to port %d", dest_port);
    while (running) {
        ssize_t bytes_sent = sendto(udp_tx_socket, message, msg_size, 0,
                                (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (bytes_sent < 0) {
            LOGE_ERRNO(TAG, "sendto() failed.");
            close(udp_tx_socket);
            return EXIT_FAILURE;
        }
        LOGI(TAG, "Sent %zd bytes to %s:%d -- Message: %.*s",
            bytes_sent, inet_ntoa(dest_addr.sin_addr),
            ntohs(dest_addr.sin_port), msg_size, message);

        if (repetitions > 0)  repetitions--;
        if (repetitions == 0) break;

        sleep(sleep_period_s);
    }
    
    if (!running) {
        LOGD(TAG, "Received shutdown signal, exiting gracefully");
    }
    close(udp_tx_socket);
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

static inline int parse_ip(const char *str, char *dest, size_t dest_size) {
    if (str == NULL || dest == NULL) {
        return -1;
    }

    size_t len = strlen(str);
    if (len == 0 || len >= dest_size) {
        return -1;
    }

    struct in_addr addr;
    if (inet_pton(AF_INET, str, &addr) != 1) {
        return -1;
    }

    strncpy(dest, str, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return 0;
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

