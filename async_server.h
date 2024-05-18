#ifndef ASYNC_SERVER_H
#define ASYNC_SERVER_H

#include "common.h"
#include "config.h"
#include "queue.h"
#include "server_common.h"

#include <stdbool.h>

typedef struct acl_context_t
{
  serv_context_t *context;
  task_t registry[QUEUE_SIZE];
  queue_t registry_idx;
  bool registry_used[QUEUE_SIZE]; /* TODO */
  int socket_fd;
  unsigned char ref_count;
  pthread_mutex_t mutex;
} acl_context_t;

bool run_async_server (task_t *, config_t *);

#endif // ASYNC_SERVER_H
