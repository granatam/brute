#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"
#include "config.h"
#include "single.h"

#include <stdbool.h>

typedef void (*task_callback_t) (task_t *, config_t *, st_context_t *);

typedef struct client_context_t
{
  task_t *task;
  config_t *config;
  task_callback_t task_callback;
} client_context_t;

bool run_client (config_t *, task_callback_t task_callback);
void find_password (task_t *, config_t *, st_context_t *);
void spawn_clients (config_t *config, task_callback_t task_callback);

#endif // CLIENT_H
