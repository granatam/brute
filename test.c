#include <pthread.h>
#include <stdio.h>

int main() {
    pthread_mutex_t mutex;
    if (pthread_mutex_init (&mutex, NULL) != 0) {
        fprintf (stderr, "Could not initialize a mutex\n");
    }

    if (pthread_mutex_lock (&mutex) != 0) {
        fprintf (stderr, "Could not lock a mutex\n");
    }

    fprintf (stderr, "FreeBSD test\n");

    if (pthread_mutex_unlock (&mutex) != 0) {
        fprintf (stderr, "Could not unlock a mutex\n");
    }
}