#ifndef ASYNC_CLIENT_H
#define ASYNC_CLIENT_H

#include "common.h"
#include "config.h"
#include "queue.h"
#include "single.h"
#include "thread_pool.h"

#include <stdbool.h>

typedef status_t (*task_callback_t) (int, task_t *, config_t *,
                                     st_context_t *);

typedef struct async_client_context_t
{
  int socket_fd;
  thread_pool_t thread_pool;
  queue_t task_queue;
  queue_t result_queue;
  pthread_mutex_t mutex;
  config_t *config;
  bool done;
  pthread_cond_t cond_sem;
} async_client_context_t;

bool run_async_client (task_t *, config_t *);
status_t find_password (int socket_fd, task_t *, config_t *, st_context_t *);

#endif // ASYNC_CLIENT_H
