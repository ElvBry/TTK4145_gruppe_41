#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <stdint.h>

#include <rtsystem/core/fifo_queue.h>

int fifo_queue_init(fifo_queue_t* queue, size_t item_size, size_t capacity) {
    queue->buffer = malloc(item_size * capacity);
    if (!queue->buffer) {
        return -1;
    }

    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

    // Initialize mutex with priority inheritance
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&queue->lock, &attr);
    pthread_mutexattr_destroy(&attr);

    // Create eventfd for poll integration
    // EFD_SEMAPHORE: each read decrements by 1
    queue->event_fd = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    if (queue->event_fd == -1) {
        free(queue->buffer);
        pthread_mutex_destroy(&queue->lock);
        return -1;
    }

    return 0;
}

void fifo_queue_destroy(fifo_queue_t* queue) {
    if (queue->buffer) {
        free(queue->buffer);
        queue->buffer = NULL;
    }
    if (queue->event_fd != -1) {
        close(queue->event_fd);
        queue->event_fd = -1;
    }
    pthread_mutex_destroy(&queue->lock);
}

int fifo_queue_send(fifo_queue_t* queue, const void* item) {
    pthread_mutex_lock(&queue->lock);

    if (queue->count >= queue->capacity) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    void* dest = (char*)queue->buffer + (queue->head * queue->item_size);
    memcpy(dest, item, queue->item_size);
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;

    // Signal eventfd that data is available
    const uint64_t val = 1;
    write(queue->event_fd, &val, sizeof(val));

    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int fifo_queue_receive(fifo_queue_t* queue, void* item) {
    pthread_mutex_lock(&queue->lock);

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    void* src = (char*)queue->buffer + (queue->tail * queue->item_size);
    memcpy(item, src, queue->item_size);
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;

    // Decrement eventfd counter
    uint64_t val;
    read(queue->event_fd, &val, sizeof(val));

    pthread_mutex_unlock(&queue->lock);
    return 0;
}

size_t fifo_queue_count(fifo_queue_t* queue) {
    pthread_mutex_lock(&queue->lock);
    size_t count = queue->count;
    pthread_mutex_unlock(&queue->lock);
    return count;
}
