#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"
#include "config.h"
#include "single.h"

#include <stdbool.h>

typedef status_t (*task_callback_t) (int, task_t *, config_t *,
                                     st_context_t *);

bool run_client (task_t *, config_t *, task_callback_t task_callback);
status_t find_password (int socket_fd, task_t *, config_t *, st_context_t *);

#endif // CLIENT_H
