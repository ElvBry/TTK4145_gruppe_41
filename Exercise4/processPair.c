#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>

#define COUNT_NUM 20

int main() {
    printf("Program started\n");
    int16_t my_port = 8080;
    errno = 0;
    int count = 0;
    int udp_rx_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_rx_socket < 0) {
        perror("Could not open socket: ");
        return EXIT_FAILURE;
    }

    struct sockaddr_in my_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(my_port),
    };

    int err = bind(udp_rx_socket, (struct sockaddr *)&my_addr, sizeof(my_addr));
    if (err < 0) {
        perror("Could not bind socket to port: ");
        return EXIT_FAILURE;
    }

    struct timeval timeout = { .tv_sec = 1, .tv_usec = 0};
    err = setsockopt(udp_rx_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (err < 0) {
        perror("Could not set socket timeout");
        return EXIT_FAILURE;
    }

    char buffer[32];
    for (;;) {
        if (count >= COUNT_NUM) {
            return EXIT_SUCCESS;
        }
        ssize_t n = recvfrom(udp_rx_socket, buffer, sizeof(buffer)- 1, 0, NULL, NULL);

        if (n > 0) {
            buffer[n] = '\0';
            count = atoi(buffer);
            printf("Backup: received count %d\n", count);
            continue;
        } 
        
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Timeout! Becoming primary...\n");
            errno = 0;
            break;
        }
        
        perror("recfrom error: ");
        return EXIT_FAILURE;
    }
   
    err = close(udp_rx_socket);
    if (err < 0) {
        perror("could not close socket: ");
        return EXIT_FAILURE;
    }

    system("gnome-terminal -- ./processPair");    
    int udp_tx_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_tx_socket < 0) {
        perror("Could not create send socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port = htons(my_port),
    };

    while (count < COUNT_NUM) {
        count++;
        printf("%d\n", count);
        fflush(stdout);
        snprintf(buffer, sizeof(buffer), "%d", count);
        sendto(udp_tx_socket, buffer, strlen(buffer), 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        usleep(500000);  // 500ms
    }

    return EXIT_SUCCESS;
}