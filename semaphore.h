#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <pthread.h>

typedef struct sem_t
{
  pthread_cond_t cond_sem;
  pthread_mutex_t mutex;
  int counter;
} sem_t;

int sem_init (sem_t *sem, int pshared, unsigned int value);
int sem_post (sem_t *sem);
int sem_wait (sem_t *sem);

#endif // SEMAPHORE_H
