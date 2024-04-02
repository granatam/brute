#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#include "config.h"
#include "multi.h"
#include "queue.h"
#include "thread_pool.h"

#include <stdbool.h>

// TODO: add comments with explanation of these contexts usage

typedef struct serv_context_t
{
  mt_context_t context;
  int socket_fd;
} serv_context_t;

// TODO: find out why we need this mutex
typedef struct cl_context_t
{
  serv_context_t *context;
  int socket_fd;
  pthread_mutex_t mutex;
} cl_context_t;

bool run_server (task_t *, config_t *);

#endif // SERVER_H
