#ifndef SYNC_SERVER_H
#define SYNC_SERVER_H

#include "common.h"
#include "config.h"
#include "server_common.h"

#include <stdbool.h>

typedef struct cl_context_t
{
  serv_context_t *context;
  int socket_fd;
} cl_context_t;

bool run_server (task_t *, config_t *);

#endif // SYNC_SERVER_H
