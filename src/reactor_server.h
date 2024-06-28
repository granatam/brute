#ifndef REACTOR_SERVER_H
#define REACTOR_SERVER_H

#include "common.h"
#include "config.h"
#include "queue.h"
#include "server_common.h"

#include <stdbool.h>

typedef struct rsrv_context_t
{
  serv_context_t *context;
} rsrv_context_t;

bool run_reactor_server (task_t *, config_t *);

#endif // REACTOR_SERVER_H
