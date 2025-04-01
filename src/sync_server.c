#include "sync_server.h"

#include "log.h"
#include "multi.h"
#include "server_common.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <sys/socket.h>
#include <sys/types.h>
#endif

typedef struct client_context_t
{
  srv_base_context_t *srv_base;
  int socket_fd;
} client_context_t;

static status_t
delegate_task (int socket_fd, task_t *task, mt_context_t *ctx)
{
  if (send_task (socket_fd, task) == S_FAILURE)
    return (S_FAILURE);

  result_t task_result;
  if (recv_wrapper (socket_fd, &task_result, sizeof (task_result), 0)
      == S_FAILURE)
    {
      error ("Could not receive result from client");
      return (S_FAILURE);
    }

  trace ("Received result from client");

  if (task_result.is_correct)
    {
      if (queue_cancel (&ctx->queue) == QS_FAILURE)
        {
          error ("Could not cancel a queue");
          return (S_FAILURE);
        }
      memcpy (ctx->password, task_result.password,
              sizeof (task_result.password));

      trace ("Received result is correct");
    }

  return (S_SUCCESS);
}

static void *
handle_client (void *arg)
{
  client_context_t *client_ctx = arg;
  mt_context_t *mt_ctx = &client_ctx->srv_base->mt_ctx;

  if (send_config_data (client_ctx->socket_fd, mt_ctx) == S_FAILURE)
    return (NULL);

  while (true)
    {
      task_t task;
      if (queue_pop (&mt_ctx->queue, &task) != QS_SUCCESS)
        return (NULL);

      task.to = task.from;
      task.from = 0;

      if (delegate_task (client_ctx->socket_fd, &task, mt_ctx) == S_FAILURE)
        {
          if (queue_push (&mt_ctx->queue, &task) == QS_FAILURE)
            error ("Could not push to the queue");

          trace ("Pushed task back to queue because of client failure");

          return (NULL);
        }

      if (srv_trysignal (mt_ctx) == S_FAILURE)
        return (NULL);
    }

  return (NULL);
}

static void *
handle_clients (void *arg)
{
  srv_base_context_t *srv_base = *(srv_base_context_t **)arg;
  mt_context_t *mt_ctx = &srv_base->mt_ctx;

  client_context_t client_ctx = {
    .srv_base = srv_base,
  };

  while (true)
    {
      /* TODO: Probably we should not continue here */
      if (accept_client (srv_base->socket_fd, &client_ctx.socket_fd)
          == S_FAILURE)
        continue;

      if (!thread_create (&mt_ctx->thread_pool, handle_client, &client_ctx,
                          sizeof (client_ctx), "sync handler"))
        {
          error ("Could not create client thread");

          close_client (client_ctx.socket_fd);
          continue;
        }
    }

  return (NULL);
}

bool
run_server (task_t *task, config_t *config)
{
  srv_base_context_t srv_base;
  srv_base_context_t *base_ptr = &srv_base;

  if (srv_base_context_init (base_ptr, config) == S_FAILURE)
    {
      error ("Could not initialize server context");
      return (false);
    }

  if (!thread_create (&base_ptr->mt_ctx.thread_pool, handle_clients, &base_ptr,
                      sizeof (base_ptr), "sync accepter"))
    {
      error ("Could not create clients thread");
      goto fail;
    }

  mt_context_t *mt_ctx = (mt_context_t *)base_ptr;

  if (process_tasks (task, config, mt_ctx) == S_FAILURE)
    goto fail;

  if (srv_base_context_destroy (base_ptr) == S_FAILURE)
    error ("Could not destroy server context");

  trace ("Destroyed the server context");

  return (mt_ctx->password[0] != 0);

fail:
  trace ("Failed, destroying server context");

  if (srv_base_context_destroy (base_ptr) == S_FAILURE)
    error ("Could not destroy server context");

  return (false);
}
