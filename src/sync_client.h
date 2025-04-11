#ifndef CLIENT_H
#define CLIENT_H

#include "client_common.h"
#include "common.h"
#include "config.h"

#include <stdbool.h>

bool run_client (config_t *config, task_callback_t task_callback);
void sync_client_find_password (task_t *task, config_t *config,
                                st_context_t *ctx);
void spawn_clients (config_t *config, task_callback_t task_callback);

#endif // CLIENT_H
