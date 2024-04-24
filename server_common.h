#ifndef SERVER_COMMON_H
#define SERVER_COMMON_H

#include "common.h"
#include "config.h"
#include "multi.h"
#include "queue.h"
#include "thread_pool.h"

#include <stdbool.h>

typedef struct serv_context_t
{
  mt_context_t context;
  int socket_fd;
} serv_context_t;

status_t serv_context_init (serv_context_t *, config_t *);
status_t serv_context_destroy (serv_context_t *);

status_t close_client (int socket_fd);

status_t send_hash (int socket_fd, mt_context_t *);
status_t send_alph (int socket_fd, mt_context_t *);

#endif // SERVER_COMMON_H
