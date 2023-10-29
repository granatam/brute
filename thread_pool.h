#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "common.h"

#include <pthread.h>

typedef struct node_t
{
  struct node_t *prev;
  struct node_t *next;
  pthread_t thread;
} node_t;

typedef struct thread_pool_t
{
  pthread_mutex_t mutex;
  node_t threads;
} thread_pool_t;

typedef struct tp_context_t
{
  thread_pool_t *thread_pool;
  void *(*func) (void *);
  void *arg;
  pthread_mutex_t mutex;
} tp_context_t;

typedef struct thread_cleanup_context_t
{
  thread_pool_t *thread_pool;
  node_t * node;
} thread_cleanup_context_t;

status_t thread_pool_init (thread_pool_t *thread_pool);
status_t thread_create (thread_pool_t *thread_pool, void *func (void *),
                        void *arg);
status_t thread_pool_cancel (thread_pool_t *thread_pool);

#endif // THREAD_POOL_H
