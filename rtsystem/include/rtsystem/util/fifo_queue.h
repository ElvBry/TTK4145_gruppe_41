#ifndef FIFO_QUEUE_H
#define FIFO_QUEUE_H

#include <stddef.h>
#include <pthread.h>

typedef struct {
    void* buffer;
    size_t item_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t lock;
    int event_fd;  // Poll on this for POLLIN before calling receive
} fifo_queue_t;

// Initialize a FIFO queue
// Returns 0 on success, -1 on error
int fifo_queue_init(fifo_queue_t* queue, size_t item_size, size_t capacity);

// Destroy a FIFO queue and free resources
void fifo_queue_destroy(fifo_queue_t* queue);

// Send an item to the queue
// Returns 0 on success, -1 if full (queue undersized)
int fifo_queue_send(fifo_queue_t* queue, const void* item);

// Receive an item from the queue
// Returns 0 on success, -1 if empty (poll on event_fd first)
int fifo_queue_receive(fifo_queue_t* queue, void* item);

// Get current item count
size_t fifo_queue_count(fifo_queue_t* queue);

#endif
