#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <pthread.h>

typedef enum sem_status_t
{
  SS_SUCCESS,
  SS_FAILURE,
  SS_LOCKED,
} sem_status_t;

typedef struct sem_t
{
  pthread_cond_t cond_sem;
  pthread_mutex_t mutex;
  int counter;
} sem_t;

sem_status_t sem_init (sem_t *sem, int pshared, unsigned int value);
sem_status_t sem_post (sem_t *sem);
sem_status_t sem_wait (sem_t *sem);
sem_status_t sem_trywait (sem_t *sem);
sem_status_t sem_destroy (sem_t *sem);

#endif // SEMAPHORE_H
