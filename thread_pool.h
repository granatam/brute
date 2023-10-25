#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "common.h"

#include <pthread.h>

typedef struct node_t
{
  struct node_t *prev;
  struct node_t *next;
  pthread_t *thread;
} node_t;

typedef struct thread_pool_t
{
  pthread_mutex_t mutex;
  node_t *data;
} thread_pool_t;

status_t thread_pool_init (thread_pool_t *thread_pool);
status_t thread_pool_insert (thread_pool_t *thread_pool, pthread_t *thread,
                             void *func (void *), void *arg);
status_t thread_pool_remove (thread_pool_t *thread_pool, pthread_t thread);
status_t thread_pool_cancel (thread_pool_t *thread_pool);

#endif // THREAD_POOL_H
