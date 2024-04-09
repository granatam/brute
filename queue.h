#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"
#include "semaphore.h"
#include <pthread.h>

#define QUEUE_SIZE (8)

typedef enum queue_status_t
{
  QS_SUCCESS,
  QS_INACTIVE,
  QS_FAILURE
} queue_status_t;

typedef struct queue_t
{
  task_t queue[QUEUE_SIZE];
  int head, tail;
  sem_t full, empty;
  pthread_mutex_t head_mutex, tail_mutex;
  bool active;
} queue_t;

queue_status_t queue_init (queue_t *queue);
queue_status_t queue_push (queue_t *queue, task_t *task);
queue_status_t queue_pop (queue_t *queue, task_t *task);
queue_status_t queue_cancel (queue_t *queue);
queue_status_t queue_destroy (queue_t *queue);

#endif // QUEUE_H
