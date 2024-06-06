#include <pthread.h>
#include <stdio.h>

int main() {
    pthread_mutex_t mutex;
    if (pthread_mutex_init (&mutex, NULL) != 0) {
        printf ("Could not initialize a mutex\n");
    }

    if (pthread_mutex_lock (&mutex) != 0) {
        printf ("Could not lock a mutex\n");
    }

    printf ("FreeBSD test\n");

    if (pthread_mutex_unlock (&mutex) != 0) {
        printf ("Could not unlock a mutex\n");
    }
}