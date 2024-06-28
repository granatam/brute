#include "reactor_server.h"

#include "brute.h"
#include "log.h"
#include "multi.h"
#include "server_common.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO: Event handlers etc
static void *
handle_io (void *arg)
{
  assert (0 && "Not implemented yet");
}

static void *
handle_clients (void *arg)
{
  serv_context_t *srv_ctx = *(serv_context_t **)arg;
  mt_context_t *mt_ctx = &srv_ctx->context;

  while (true)
    {
      int socket_fd;
      if (accept_client (srv_ctx->socket_fd, &socket_fd) == S_FAILURE)
        return (NULL);

      // TODO: add socket fd to list of watched fds
    }

  return (NULL);
}

bool
run_reactor_server (task_t *task, config_t *config)
{
  signal (SIGPIPE, SIG_IGN);
  serv_context_t context;
  serv_context_t *context_ptr = &context;

  if (serv_context_init (&context, config) == S_FAILURE)
    {
      error ("Could not initialize server context");
      return (false);
    }

  int number_of_threads
      = (config->number_of_threads == 1) ? 1 : config->number_of_threads - 1;
  if (!create_threads (&context.context.thread_pool, number_of_threads,
                       handle_io, &context_ptr, sizeof (context_ptr),
                       "i/o handler"))
    goto fail;

  if (!thread_create (&context.context.thread_pool, handle_clients,
                      &context_ptr, sizeof (context_ptr), "clients accepter"))
    {
      error ("Could not create clients thread");
      goto fail;
    }

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  mt_context_t *mt_ctx = (mt_context_t *)&context;

  brute (task, config, queue_push_wrapper, mt_ctx);

  trace ("Calculated all tasks");

  if (wait_password (mt_ctx) == S_FAILURE)
    goto fail;

  trace ("Got password");

  if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
    {
      error ("Could not cancel a queue");
      goto fail;
    }

  trace ("Cancelled global queue");

  if (mt_ctx->password[0] != 0)
    memcpy (task->task.password, mt_ctx->password, sizeof (mt_ctx->password));

  if (serv_context_destroy (&context) == S_FAILURE)
    error ("Could not destroy server context");

  trace ("Destroyed server context");

  return (mt_ctx->password[0] != 0);

fail:
  trace ("Failed, destroying server context");

  if (serv_context_destroy (&context) == S_FAILURE)
    error ("Could not destroy server context");

  return (false);
}
