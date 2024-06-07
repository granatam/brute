#ifndef ASYNC_CLIENT_H
#define ASYNC_CLIENT_H

#include "common.h"
#include "config.h"
#include "queue.h"
#include "single.h"
#include "thread_pool.h"

#include <stdbool.h>

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
  char hash[HASH_LENGTH];
  char alph[MAX_ALPH_LENGTH];
} async_client_context_t;

bool run_async_client (config_t *);

#endif // ASYNC_CLIENT_H
