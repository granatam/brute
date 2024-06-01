#include "server_common.h"

#include "brute.h"
#include "common.h"
#include "log.h"
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
      error ("Could not initialize server socket");
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
      error ("Could not bind socket");
      goto fail;
    }

  if (listen (context->socket_fd, 10) == -1)
    {
      error ("Could not start listening to incoming connections");
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

  trace ("After mt context destroy");

  shutdown (context->socket_fd, SHUT_RDWR);
  if (close (context->socket_fd) != 0)
    {
      error ("Could not close server socket");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
close_client (int socket_fd)
{
  shutdown (socket_fd, SHUT_RDWR);
  close (socket_fd);

  trace ("Closed connection with client");

  return (S_SUCCESS);
}

status_t
send_hash (int socket_fd, mt_context_t *mt_ctx)
{
  command_t cmd = CMD_HASH;

  struct iovec vec[] = {
    { .iov_base = &cmd, .iov_len = sizeof (cmd) },
    { .iov_base = mt_ctx->config->hash, .iov_len = HASH_LENGTH },
  };

  if (send_wrapper (socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
      == S_FAILURE)
    {
      error ("Could not send hash to client");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
send_alph (int socket_fd, mt_context_t *mt_ctx)
{
  command_t cmd = CMD_ALPH;
  int32_t length = strlen (mt_ctx->config->alph);

  struct iovec vec[] = {
    { .iov_base = &cmd, .iov_len = sizeof (cmd) },
    { .iov_base = &length, .iov_len = sizeof (length) },
    { .iov_base = mt_ctx->config->alph, .iov_len = length },
  };

  if (send_wrapper (socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
      == S_FAILURE)
    {
      error ("Could not send alphabet to client");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
send_config_data (int socket_fd, mt_context_t *ctx)
{
  if (send_hash (socket_fd, ctx) == S_FAILURE)
    return (S_FAILURE);
  trace ("Sent hash to client");

  if (send_alph (socket_fd, ctx) == S_FAILURE)
    return (S_FAILURE);
  trace ("Sent alphabet to client");

  return (S_SUCCESS);
}

status_t
send_task (int socket_fd, task_t *task)
{
  command_t cmd = CMD_TASK;

  task->task.is_correct = false;
  struct iovec vec[] = { { .iov_base = &cmd, .iov_len = sizeof (cmd) },
                         { .iov_base = task, .iov_len = sizeof (*task) } };

  trace ("Sending task with from = %d and to = %d", task->from, task->to);

  if (send_wrapper (socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
      == S_FAILURE)
    {
      error ("Could not send task to client");
      return (S_FAILURE);
    }

  trace ("Sent task %s to client", task->task.password);

  return (S_SUCCESS);
}

status_t
serv_signal_if_found (mt_context_t *ctx)
{
  if (pthread_mutex_lock (&ctx->mutex) != 0)
    {
      error ("Could not lock a mutex");
      return (S_FAILURE);
    }
  status_t status = S_SUCCESS;
  pthread_cleanup_push (cleanup_mutex_unlock, &ctx->mutex);

  if (--ctx->passwords_remaining == 0 || ctx->password[0] != 0)
    {
      if (ctx->passwords_remaining == 0)
        trace ("No passwords are left, signaling now");
      else
        trace ("Password is found, signaling now");

      if (pthread_cond_signal (&ctx->cond_sem) != 0)
        {
          error ("Could not signal a condition");
          status = S_FAILURE;
        }
      trace ("Signaled on conditional semaphore");
    }

  pthread_cleanup_pop (!0);

  return (status);
}
