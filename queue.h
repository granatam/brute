#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"
#include <pthread.h>
#ifdef __APPLE__
#include "semaphore.h"
#else
#include <semaphore.h>
#endif

#define QUEUE_SIZE (8)

typedef struct queue_t
{
  task_t queue[QUEUE_SIZE];
  int head, tail;
  sem_t full, empty;
  pthread_mutex_t head_mutex, tail_mutex;
} queue_t;

int queue_init (queue_t *queue);
int queue_push (queue_t *queue, task_t *task);
int queue_pop (queue_t *queue, task_t *task);

#endif // QUEUE_H
