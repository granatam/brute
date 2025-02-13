#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "common.h"

#include <pthread.h>

typedef struct node_t
{
  struct node_t *prev;
  struct node_t *next;
  pthread_t thread;
  char *name;
} node_t;

typedef struct thread_pool_t
{
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int count;
  bool cancelled;
  node_t threads;
} thread_pool_t;

typedef struct tp_context_t
{
  thread_pool_t *thread_pool;
  void *(*func) (void *);
  void *arg;
  size_t arg_size;
  pthread_mutex_t mutex;
  char *name;
} tp_context_t;

typedef struct thread_cleanup_context_t
{
  thread_pool_t *thread_pool;
  node_t *node;
} thread_cleanup_context_t;

status_t thread_pool_init (thread_pool_t *thread_pool);
pthread_t thread_create (thread_pool_t *thread_pool, void *func (void *),
                         void *arg, size_t arg_size, char *name);
status_t thread_pool_collect (thread_pool_t *thread_pool, bool cancel);
status_t thread_pool_cancel (thread_pool_t *thread_pool);
status_t thread_pool_join (thread_pool_t *thread_pool);

int create_threads (thread_pool_t *thread_pool, int number_of_threads,
                    void *func (void *), void *context, size_t context_size,
                    char *name);

#endif // THREAD_POOL_H
