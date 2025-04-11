#ifndef CLIENT_COMMON_H
#define CLIENT_COMMON_H

#include "common.h"
#include "config.h"
#include "single.h"

#include <stdbool.h>

typedef void (*task_callback_t) (task_t *, config_t *, st_context_t *);

typedef struct client_base_context_t
{
  int socket_fd;
  task_t *task;
  config_t *config;
  task_callback_t task_cb;
  char hash[HASH_LENGTH];
  char alph[MAX_ALPH_LENGTH];
} client_base_context_t;

typedef status_t (*client_task_handler_t) (client_base_context_t *, task_t *,
                                           void *arg);

status_t client_base_context_init (client_base_context_t *client_base,
                                   config_t *config, task_callback_t task_cb);
status_t client_base_context_destroy (client_base_context_t *client_base);
status_t client_base_recv_loop (client_base_context_t *client_base,
                                task_t *task, client_task_handler_t task_hdlr,
                                void *task_hdlr_arg);

status_t handle_alph (int socket_fd, char *alph);
status_t handle_hash (int socket_fd, char *hash);
int ms_sleep (long milliseconds);

#endif // CLIENT_COMMON_H
