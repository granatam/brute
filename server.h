#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#include "config.h"
#include "multi.h"
#include "queue.h"
#include "thread_pool.h"

#include <stdbool.h>

typedef struct socket_array_t
{
  size_t size;
  size_t capacity;
  int *data;
} socket_array_t;

typedef struct serv_context_t
{
  mt_context_t context;
  int socket_fd;
  socket_array_t sock_arr;
} serv_context_t;

typedef struct cl_context_t
{
  serv_context_t *context;
  int socket_fd;
} cl_context_t;

bool run_server (task_t *, config_t *);

#endif // SERVER_H
