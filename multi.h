#ifndef MULTI_H
#define MULTI_H

#include "config.h"
#include "queue.h"
#include <stdbool.h>

typedef struct mt_context_t
{
  queue_t queue;
  config_t *config;
  password_t password;
} mt_context_t;

void *mt_password_check (void *context);
bool queue_push_wrapper (task_t *task, void *context);
bool run_multi (task_t *task, config_t *config);

#endif // MULTI_H
