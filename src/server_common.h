#ifndef SERVER_COMMON_H
#define SERVER_COMMON_H

#include "common.h"
#include "config.h"
#include "multi.h"

#include <stdbool.h>

typedef struct serv_base_context_t
{
  mt_context_t context;
  int socket_fd;
} serv_base_context_t;

status_t serv_base_context_init (serv_base_context_t *ctx, config_t *config);
status_t serv_base_context_destroy (serv_base_context_t *ctx);

status_t accept_client (int srv_socket_fd, int *cl_socket_fd);
status_t close_client (int socket_fd);

status_t send_hash (int socket_fd, mt_context_t *ctx);
status_t send_alph (int socket_fd, mt_context_t *ctx);
status_t send_config_data (int socket_fd, mt_context_t *ctx);
status_t send_task (int socket_fd, task_t *task);

status_t serv_signal_if_found (mt_context_t *ctx);

status_t process_tasks (task_t *task, config_t *config, mt_context_t *mt_ctx);

#endif // SERVER_COMMON_H
