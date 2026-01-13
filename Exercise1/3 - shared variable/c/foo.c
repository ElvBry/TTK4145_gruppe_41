// Compile with `gcc foo.c -Wall -std=gnu99 -lpthread`, or use the makefile
// The executable will be named `foo` if you use the makefile, or `a.out` if you use gcc directly

#include <pthread.h>
#include <stdio.h>

int i = -1;
pthread_mutex_t mutex; // i is a single resource that needs to be locked. Semaphore is meant for multiple resources

void* incrementingThreadFunction(){
    for (int j = 0; j < 1000000; j++) {
        pthread_mutex_lock(&mutex);
        i += 1;
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

void* decrementingThreadFunction(){
    for (int j = 0; j < 1000000; j++) {
        pthread_mutex_lock(&mutex);
        i -= 1;
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main(){
    pthread_t IncThread, DecThread;
    pthread_mutex_init(&mutex, NULL);
    pthread_create(&IncThread, NULL, incrementingThreadFunction, NULL);
    pthread_create(&DecThread, NULL, decrementingThreadFunction, NULL);

    pthread_join(IncThread, NULL);
    pthread_join(DecThread, NULL);
    pthread_mutex_destroy(&mutex);
    
    printf("The magic number is: %d\n", i);
    return 0;
}
