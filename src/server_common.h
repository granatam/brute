#ifndef SERVER_COMMON_H
#define SERVER_COMMON_H

#include "common.h"
#include "config.h"

typedef struct srv_listener_t
{
  int listen_fd;
} srv_listener_t;

status_t srv_listener_init (srv_listener_t *listener, config_t *config);
status_t srv_listener_destroy (srv_listener_t *listener);

status_t accept_client (int srv_socket_fd, int *client_socket_fd);

status_t send_hash (int socket_fd, config_t *config);
status_t send_alph (int socket_fd, config_t *config);
status_t send_config_data (int socket_fd, config_t *config);
status_t send_task (int socket_fd, task_t *task);

#endif
