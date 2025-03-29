#ifndef QUEUE_H
#define QUEUE_H

#include "semaphore.h"
#include <pthread.h>

#define QUEUE_SIZE (8)

typedef enum queue_status_t
{
  QS_SUCCESS,
  QS_INACTIVE,
  QS_FAILURE,
  QS_EMPTY,
} queue_status_t;

typedef struct ll_node_t
{
  struct ll_node_t *prev;
  struct ll_node_t *next;
  char payload[1];
} ll_node_t;

typedef struct queue_t
{
  void *queue;
  size_t unit_size;
  int head, tail;
  sem_t full, empty;
  pthread_mutex_t head_mutex, tail_mutex;
  bool active;
  ll_node_t list;
} queue_t;

queue_status_t queue_init (queue_t *queue, size_t unit_size);
queue_status_t queue_push (queue_t *queue, void *payload);
queue_status_t queue_push_back (queue_t *queue, void *payload);
queue_status_t queue_pop (queue_t *queue, void *payload);
queue_status_t queue_trypop (queue_t *queue, void *payload);
queue_status_t queue_cancel (queue_t *queue);
queue_status_t queue_destroy (queue_t *queue);

#endif // QUEUE_H
