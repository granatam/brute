#include "server_common.h"

#include "brute.h"
#include "common.h"
#include "multi.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

status_t
serv_context_init (serv_context_t *context, config_t *config)
{
  if (mt_context_init ((mt_context_t *)context, config) == S_FAILURE)
    return (S_FAILURE);

  if ((context->socket_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      print_error ("Could not initialize server socket\n");
      return (S_FAILURE);
    }

  int option = 1;
  setsockopt (context->socket_fd, SOL_SOCKET, SO_REUSEADDR, &option,
              sizeof (option));

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr (config->addr);
  serv_addr.sin_port = htons (config->port);

  if (bind (context->socket_fd, (struct sockaddr *)&serv_addr,
            sizeof (serv_addr))
      == -1)
    {
      print_error ("Could not bind socket\n");
      goto fail;
    }

  if (listen (context->socket_fd, 10) == -1)
    {
      print_error ("Could not start listening connections\n");
      goto fail;
    }

  return (S_SUCCESS);

fail:
  close (context->socket_fd);
  return (S_FAILURE);
}

status_t
serv_context_destroy (serv_context_t *context)
{
  if (mt_context_destroy ((mt_context_t *)context) == S_FAILURE)
    return (S_FAILURE);

  shutdown (context->socket_fd, SHUT_RDWR);
  if (close (context->socket_fd) != 0)
    {
      print_error ("Could not close server socket\n");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
close_client (int socket_fd)
{
  command_t cmd = CMD_EXIT;
  if (send_wrapper (socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
    {
      print_error ("Could not send CMD_EXIT to client\n");
      return (S_FAILURE);
    }

  shutdown (socket_fd, SHUT_RDWR);
  close (socket_fd);

  return (S_SUCCESS);
}

status_t
send_hash (int socket_fd, mt_context_t *mt_ctx)
{
  command_t cmd = CMD_HASH;
  if (send_wrapper (socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
    {
      print_error ("Could not send CMD_HASH to client\n");
      return (S_FAILURE);
    }

  if (send_wrapper (socket_fd, mt_ctx->config->hash, HASH_LENGTH, 0)
      == S_FAILURE)
    {
      print_error ("Could not send hash to client\n");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
send_alph (int socket_fd, mt_context_t *mt_ctx)
{
  command_t cmd = CMD_ALPH;
  if (send_wrapper (socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
    {
      print_error ("Could not send CMD_ALPH to client\n");
      return (S_FAILURE);
    }

  int32_t length = strlen (mt_ctx->config->alph);
  if (send_wrapper (socket_fd, &length, sizeof (length), 0) == S_FAILURE)
    {
      print_error ("Could not send alphabet length to client\n");
      return (S_FAILURE);
    }

  if (send_wrapper (socket_fd, mt_ctx->config->alph, length, 0) == S_FAILURE)
    {
      print_error ("Could not send alphabet to client\n");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
serv_signal_if_found (int socket_fd, mt_context_t *ctx)
{
  if (pthread_mutex_lock (&ctx->mutex) != 0)
    {
      print_error ("Could not lock a mutex\n");
      return (S_FAILURE);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &ctx->mutex);

  if (--ctx->passwords_remaining == 0 || ctx->password[0] != 0)
    {
      close_client (socket_fd);

      if (pthread_cond_signal (&ctx->cond_sem) != 0)
        {
          print_error ("Could not signal a condition\n");
          return (S_FAILURE);
        }
    }

  pthread_cleanup_pop (!0);
}
