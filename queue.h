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
  bool active;
} queue_t;

status_t queue_init (queue_t *queue);
status_t queue_push (queue_t *queue, task_t *task);
status_t queue_pop (queue_t *queue, task_t *task);
status_t queue_cancel (queue_t *queue);
status_t queue_destroy (queue_t *queue);

#endif // QUEUE_H
