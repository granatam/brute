#include "sync_server.h"

#include "brute.h"
#include "brute_engine.h"
#include "log.h"
#include "queue.h"
#include "server_common.h"
#include "thread_pool.h"

#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

typedef struct sync_server_context_t
{
  srv_listener_t listener;
  brute_engine_t engine;
  thread_pool_t thread_pool;
  config_t *config;
} sync_server_context_t;

typedef struct client_context_t
{
  sync_server_context_t *srv;
  int socket_fd;
} client_context_t;

static status_t
sync_server_context_init (sync_server_context_t *server, config_t *config)
{
  server->config = config;

  if (srv_listener_init (&server->listener, config) == S_FAILURE)
    return S_FAILURE;

  if (brute_engine_init (&server->engine) == S_FAILURE)
    goto cleanup_listener;

  if (thread_pool_init (&server->thread_pool) == S_FAILURE)
    goto cleanup_engine;

  return S_SUCCESS;

cleanup_engine:
  brute_engine_destroy (&server->engine);

cleanup_listener:
  srv_listener_destroy (&server->listener);

  return S_FAILURE;
}

static void
sync_server_context_stop (sync_server_context_t *server)
{
  if (brute_engine_cancel (&server->engine) == S_FAILURE)
    error ("Could not cancel brute engine");

  if (srv_listener_destroy (&server->listener) == S_FAILURE)
    error ("Could not destroy server listener");
}

static void
sync_server_context_destroy (sync_server_context_t *server)
{
  if (brute_engine_destroy (&server->engine) == S_FAILURE)
    error ("Could not destroy brute engine");
}

static status_t
delegate_task (sync_server_context_t *srv, int socket_fd, task_t *task)
{
  if (send_task (socket_fd, task) == S_FAILURE)
    return (S_FAILURE);

  result_t task_result;

  io_status_t recv_status
      = recv_wrapper (socket_fd, &task_result, sizeof (task_result), 0);
  if (recv_status != IOS_SUCCESS)
    {
      if (recv_status == IOS_FAILURE)
        error ("Could not receive result from client");
      return (S_FAILURE);
    }

  trace ("Received result from client");

  if (task_result.is_correct)
    {
      if (brute_engine_report_result (&srv->engine, task_result.password)
          == S_FAILURE)
        return (S_FAILURE);

      trace ("Received result is correct");
    }

  return (S_SUCCESS);
}

static void *
handle_client (void *arg)
{
  client_context_t *client_ctx = arg;
  sync_server_context_t *srv = client_ctx->srv;

  if (send_config_data (client_ctx->socket_fd, srv->config) == S_FAILURE)
    return (NULL);

  while (true)
    {
      task_t task;
      if (brute_engine_take_task (&srv->engine, &task) != QS_SUCCESS)
        return (NULL);

      task.to = task.from;
      task.from = 0;

      if (delegate_task (srv, client_ctx->socket_fd, &task) == S_FAILURE)
        {
          if (brute_engine_return_task (&srv->engine, &task) != QS_SUCCESS)
            error ("Could not return task to brute engine");

          trace ("Returned task because of client failure");
          return NULL;
        }

      if (brute_engine_task_done (&srv->engine) == S_FAILURE)
        return NULL;
    }

  return (NULL);
}

static void *
handle_clients (void *arg)
{
  sync_server_context_t *srv = *(sync_server_context_t **)arg;

  client_context_t client_ctx = {
    .srv = srv,
  };

  while (true)
    {
      if (accept_client (srv->listener.listen_fd, &client_ctx.socket_fd)
          == S_FAILURE)
        break;

      if (!thread_create (&srv->thread_pool, handle_client, &client_ctx,
                          sizeof (client_ctx), "sync handler"))
        {
          error ("Could not create client thread");

          shutdown (client_ctx.socket_fd, SHUT_RDWR);
          if (close (client_ctx.socket_fd) != 0)
            error ("Could not close client socket");

          continue;
        }
    }

  return (NULL);
}

static bool
submit_task_cb (task_t *task, void *context)
{
  brute_engine_t *engine = context;

  queue_status_t status = brute_engine_submit_task (engine, task);
  if (status == QS_FAILURE)
    {
      error ("Could not submit task to brute engine");
      return false;
    }

  return brute_engine_has_result (engine);
}

bool
run_server (task_t *task, config_t *config)
{
  sync_server_context_t srv;
  sync_server_context_t *srv_ptr = &srv;
  bool found = false;

  if (sync_server_context_init (srv_ptr, config) == S_FAILURE)
    {
      error ("Could not initialize server context");
      return (false);
    }

  if (!thread_create (&srv_ptr->thread_pool, handle_clients, &srv_ptr,
                      sizeof (srv_ptr), "sync accepter"))
    {
      error ("Could not create clients thread");
      goto cleanup;
    }

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  brute (task, config, submit_task_cb, &srv.engine);

  trace ("Calculated all tasks");

  if (brute_engine_wait (&srv.engine) == S_FAILURE)
    goto cleanup;

  found = brute_engine_copy_result (&srv.engine, task->result.password);

cleanup:
  sync_server_context_stop (&srv);

  if (thread_pool_join (&srv.thread_pool) == S_FAILURE)
    error ("Could not join thread pool");

  sync_server_context_destroy (&srv);

  return found;
}
