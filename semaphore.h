#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "common.h"
#include <pthread.h>

typedef struct sem_t
{
  pthread_cond_t cond_sem;
  pthread_mutex_t mutex;
  int counter;
} sem_t;

status_t sem_init (sem_t *sem, int pshared, unsigned int value);
status_t sem_post (sem_t *sem);
status_t sem_wait (sem_t *sem);

#endif // SEMAPHORE_H
