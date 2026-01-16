#include <netinet/in.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

#define LOG_LEVEL LOG_LEVEL_DEBUG
#include "log_helper/log_helper.h"

#define DEFAULT_PORT 8080
#define DEFAULT_BUFFER_SIZE 1024

#define REM_TRAIL // optional macro to remove trailing newline

static const char *TAG = "udp_receiver";

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static inline int parse_port(const char *str);
static inline int parse_size(const char *str);
static inline int parse_repetitions(const char *str);

#define CHECK(result, tag, fmt, ...) \
    do { if ((result) < 0) { \
        LOGE(tag, fmt, ##__VA_ARGS__); \
        return EXIT_FAILURE; \
    } } while(0)

#define HELP_MSG() \
    LOGI(TAG, "[-h (this message)] [-p <port 1-65535>] [-s <buffer_size>] [-r <repetitions -1-%d> (infinite by default)] \n", INT_MAX)


int main(int argc, char **argv) {
    int buffer_size = DEFAULT_BUFFER_SIZE;
    int my_port = DEFAULT_PORT;
    int repetitions = -1; //infinite by default
    int opt;
    while ((opt = getopt(argc, argv, "p:s:r:h")) != -1) {
        switch (opt) {
            case 'p':
                my_port = parse_port(optarg);
                CHECK(my_port, TAG, "Invalid port: %s (must be between 1-65535)", optarg);
                break;
            case 's':
                buffer_size = parse_size(optarg);
                CHECK(buffer_size, TAG, "Invalid size: %s (must be between 1-65507)", optarg);
                break;
            case 'r':
                repetitions = parse_repetitions(optarg);
                if (repetitions != -1) CHECK(repetitions, TAG, "Invalid repetition: %s (must be between -1-%d)", optarg, INT_MAX);
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
    LOGD(TAG, "buffer size: %d, port: %d, repetitions: %d", buffer_size, my_port, repetitions);

    int udp_rx_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_rx_socket < 0) {
        LOGE_ERRNO(TAG, "Failed to create socket");
        return EXIT_FAILURE;
    }

    LOGD(TAG, "Created rx socket");

    struct sockaddr_in my_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(my_port)
    };

    int result = bind(udp_rx_socket, (struct sockaddr *)&my_addr, sizeof(my_addr));
    if (result < 0) {
        LOGE_ERRNO(TAG, "Could not bind socket to port %d", my_port);
        close(udp_rx_socket);
        return EXIT_FAILURE;
    }
    LOGD(TAG, "Bound socket to port");

    struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = 0 // Allow for interrupt
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    LOGI(TAG, "Listening on port %d", my_port);

    struct sockaddr_in peer_addr;
    socklen_t addr_len = sizeof(peer_addr);
    char *rx_buf = malloc(buffer_size);
    if (rx_buf == NULL) {
        LOGE(TAG, "Failed to allocate receive buffer of size: %d", buffer_size);
        close(udp_rx_socket);
        return EXIT_FAILURE;
    }
    while (running) {
        addr_len = sizeof(peer_addr);
        ssize_t bytes_received = recvfrom(udp_rx_socket, rx_buf, buffer_size - 1, MSG_TRUNC,
                                          (struct sockaddr *)&peer_addr, &addr_len);

        if (bytes_received < 0) {
            if (errno == EINTR) {
                continue;  // interrupted by signal, check running flag
            }
            LOGE_ERRNO(TAG, "recvfrom() failed.");
            free(rx_buf);
            close(udp_rx_socket);
            return EXIT_FAILURE;
        }

        if (bytes_received > buffer_size - 1) {
            LOGW(TAG, "Message truncated: received %zd, buffer only %d",
                 bytes_received, buffer_size - 1);
        }

        rx_buf[bytes_received] = '\0';

        #ifdef REM_TRAIL
        while (bytes_received > 0 &&
            (rx_buf[bytes_received - 1] == '\n' || rx_buf[bytes_received - 1] == '\r')) {
            rx_buf[--bytes_received] = '\0';
        }
        #endif

        LOGI(TAG, "Received %zd bytes from %s:%d -- Message: %s",
            bytes_received, inet_ntoa(peer_addr.sin_addr),
            ntohs(peer_addr.sin_port), rx_buf);

        if (repetitions > 0) repetitions--;
        if (repetitions == 0) break;
    }
    

    if (!running) {
        LOGD(TAG, "Received shutdown signal, exiting gracefully");
    }
    close(udp_rx_socket);
    free(rx_buf);
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
    
    if (size > 65507 || size < 1) {
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