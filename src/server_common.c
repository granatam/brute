#include "server_common.h"

#include "brute.h"
#include "common.h"
#include "log.h"
#include "multi.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

status_t
srv_base_context_init (srv_base_context_t *srv_base, config_t *config)
{
  if (mt_context_init ((mt_context_t *)srv_base, config) == S_FAILURE)
    return (S_FAILURE);

  if ((srv_base->listen_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      error ("Could not initialize server socket");
      return (S_FAILURE);
    }

  int option = 1;
  if (setsockopt (srv_base->listen_fd, SOL_SOCKET, SO_REUSEADDR, &option,
                  sizeof (option))
      == -1)
    {
      error ("Could not set socket option");
      goto fail;
    }

  struct sockaddr_in srv_addr;
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_addr.s_addr = inet_addr (config->addr);
  srv_addr.sin_port = htons (config->port);

  if (bind (srv_base->listen_fd, (struct sockaddr *)&srv_addr,
            sizeof (srv_addr))
      == -1)
    {
      error ("Could not bind socket");
      goto fail;
    }

  if (listen (srv_base->listen_fd, 10) == -1)
    {
      error ("Could not start listening to incoming connections");
      goto fail;
    }

  return (S_SUCCESS);

fail:
  close (srv_base->listen_fd);
  return (S_FAILURE);
}

status_t
srv_base_context_destroy (srv_base_context_t *srv_base)
{
  if (mt_context_destroy ((mt_context_t *)srv_base) == S_FAILURE)
    return (S_FAILURE);

  if (srv_base->listen_fd >= 0)
    {
      shutdown (srv_base->listen_fd, SHUT_RDWR);
      if (close (srv_base->listen_fd) != 0)
        {
          error ("Could not close server socket");
          return (S_FAILURE);
        }
      srv_base->listen_fd = -1;
    }

  return (S_SUCCESS);
}

status_t
accept_client (int srv_socket_fd, int *client_socket_fd)
{
  while (true)
    {
      if (srv_socket_fd < 0)
        {
          error ("Invalid server socket");
          return (S_FAILURE);
        }

      *client_socket_fd = accept (srv_socket_fd, NULL, NULL);
      if (*client_socket_fd == -1)
        {
          error ("Could not accept new connection: %s", strerror (errno));
          if (errno == EINVAL)
            return (S_FAILURE);

          continue;
        }
      break;
    }

  trace ("Accepted new connection");

  int option = 1;
  if (setsockopt (*client_socket_fd, IPPROTO_TCP, TCP_NODELAY, &option,
                  sizeof (option))
      == -1)
    {
      error ("Could not set socket option");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
close_client (int socket_fd)
{
  shutdown (socket_fd, SHUT_RDWR);
  if (close (socket_fd) != 0)
    {
      error ("Could not close client socket");
      return (S_FAILURE);
    }

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

  task->result.is_correct = false;
  struct iovec vec[] = { { .iov_base = &cmd, .iov_len = sizeof (cmd) },
                         { .iov_base = task, .iov_len = sizeof (*task) } };

  if (send_wrapper (socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
      == S_FAILURE)
    {
      error ("Could not send task to client");
      return (S_FAILURE);
    }

  trace ("Sent task %s to client", task->result.password);

  return (S_SUCCESS);
}

status_t
srv_trysignal (mt_context_t *ctx)
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
      trace (ctx->passwords_remaining == 0
                 ? "No passwords are left, signaling now"
                 : "Password is found, signaling now");

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

status_t
process_tasks (task_t *task, config_t *config, mt_context_t *mt_ctx)
{
  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  brute (task, config, queue_push_wrapper, mt_ctx);

  trace ("Calculated all tasks");

  if (wait_password (mt_ctx) == S_FAILURE)
    return (S_FAILURE);

  trace ("Got password");

  if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
    {
      error ("Could not cancel a queue");
      return (S_FAILURE);
    }

  trace ("Cancelled global queue");

  if (mt_ctx->password[0] != 0)
    memcpy (task->result.password, mt_ctx->password,
            sizeof (mt_ctx->password));

  return (S_SUCCESS);
}
