#ifndef MULTI_H
#define MULTI_H

#include "config.h"
#include "queue.h"
#include "thread_pool.h"

#include <stdbool.h>

typedef struct mt_context_t
{
  queue_t queue;
  config_t *config;
  password_t password;
  volatile int passwords_remaining;
  pthread_mutex_t mutex;
  pthread_cond_t cond_sem;
  thread_pool_t thread_pool;
} mt_context_t;

status_t mt_context_init (mt_context_t *context, config_t *config);
status_t mt_context_destroy (mt_context_t *context);

void *mt_password_check (void *context);
bool queue_push_wrapper (task_t *task, void *context);
status_t signal_if_found (mt_context_t *ctx);
status_t wait_password (mt_context_t *ctx);
bool run_multi (task_t *task, config_t *config);

#endif // MULTI_H
