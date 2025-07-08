#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include "common.h"

#include <pthread.h>

typedef int (*pq_comparator_t) (const void *lhs, const void *rhs);

typedef struct priority_queue_t
{
  void *heap;
  size_t size;
  size_t cap;
  pq_comparator_t cmp;
  size_t unit_size;
  pthread_mutex_t mutex;
} priority_queue_t;

status_t priority_queue_init (priority_queue_t *pq, size_t cap,
                              size_t unit_size, pq_comparator_t cmp);
status_t priority_queue_push (priority_queue_t *pq, void *payload);
status_t priority_queue_top (priority_queue_t *pq, void *payload);
status_t priority_queue_pop (priority_queue_t *pq);
void priority_queue_destroy (priority_queue_t *pq);

#endif /* PRIORITY_QUEUE_H */
