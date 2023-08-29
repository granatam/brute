#ifndef MULTI_H
#define MULTI_H

#include "config.h"
#include "queue.h"
#include <stdbool.h>

typedef struct mt_context_t
{
  queue_t queue;
  config_t *config;
  password_t result;
} mt_context_t;

bool mt_password_handler(password_t password, void *context);
bool run_multi (password_t password, config_t *config);

#endif // MULTI_H
