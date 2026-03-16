#include <assert.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <pthread.h>

#include <rtsystem/rtsystem_config.h>
#include "rtsystem/core/elevator_hardware.h"

#define LOG_LEVEL LOG_LEVEL_ELEVATOR_HARDWARE
#ifdef ASYNC_LOG
    #include <rtsystem/async_log_helper.h>
#else
    #include <rtsystem/log_helper.h>
#endif

const static char *TAG = "elevator_hw";

static int sockfd;
static pthread_mutex_t sockmtx;

int elevator_hardware_init() {
    char ip[16]  = ELEVATOR_HARDWARE_IP;
    char port[8] = ELEVATOR_HARDWARE_PORT;
    
    pthread_mutex_init(&sockmtx, NULL);

    errno = 0;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        LOGE_ERRNO(TAG, "could not set up socket with error: ");
        return -1;
    }
    
    struct addrinfo hints = {
        .ai_family      = AF_INET, 
        .ai_socktype    = SOCK_STREAM, 
        .ai_protocol    = IPPROTO_TCP,
    };
    struct addrinfo* res;
    errno = 0;
    int err = getaddrinfo(ip, port, &hints, &res);
    if (err != 0) {
        if (err == EAI_SOCKTYPE) {
            LOGE_ERRNO(TAG, "system error: ");
            return -1;
        }
        LOGE(TAG, "could not get addrinfo with error: %s",gai_strerror(err));
        return -1;
    }
    errno = 0;
    err = connect(sockfd, res->ai_addr, res->ai_addrlen);
    if (err != 0) {
        LOGE_ERRNO(TAG, "count not connect socket with error: ");
        return -1;
    }    
    freeaddrinfo(res);
    
    send(sockfd, (char[4]) {0}, 4, 0);
    return 0;
}




void elevator_hardware_set_motor_direction(elevator_hardware_motor_direction_t dirn) {
    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {1, dirn}, 4, 0);
    pthread_mutex_unlock(&sockmtx);
}


void elevator_hardware_set_button_lamp(elevator_hardware_button_type_t button, int floor, int value) {
    assert(floor >= 0);
    assert(floor < N_FLOORS);
    assert(button >= 0);
    assert(button < N_BUTTONS);

    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {2, button, floor, value}, 4, 0);
    pthread_mutex_unlock(&sockmtx);
}


void elevator_hardware_set_floor_indicator(int floor) {
    assert(floor >= 0);
    assert(floor < N_FLOORS);

    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {3, floor}, 4, 0);
    pthread_mutex_unlock(&sockmtx);
}


void elevator_hardware_set_door_open_lamp(int value) {
    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {4, value}, 4, 0);
    pthread_mutex_unlock(&sockmtx);
}


void elevator_hardware_set_stop_lamp(int value) {
    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {5, value}, 4, 0);
    pthread_mutex_unlock(&sockmtx);
}




int elevator_hardware_get_button_signal(elevator_hardware_button_type_t button, int floor) {
    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {6, button, floor}, 4, 0);
    char buf[4];
    recv(sockfd, buf, 4, 0);
    pthread_mutex_unlock(&sockmtx);
    return buf[1];
}


int elevator_hardware_get_floor_sensor_signal(void) {
    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {7}, 4, 0);
    char buf[4];
    recv(sockfd, buf, 4, 0);
    pthread_mutex_unlock(&sockmtx);
    return buf[1] ? buf[2] : -1;
}


int elevator_hardware_get_stop_signal(void) {
    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {8}, 4, 0);
    char buf[4];
    recv(sockfd, buf, 4, 0);
    pthread_mutex_unlock(&sockmtx);
    return buf[1];
}


int elevator_hardware_get_obstruction_signal(void) {
    pthread_mutex_lock(&sockmtx);
    send(sockfd, (char[4]) {9}, 4, 0);
    char buf[4];
    recv(sockfd, buf, 4, 0);
    pthread_mutex_unlock(&sockmtx);
    return buf[1];
}