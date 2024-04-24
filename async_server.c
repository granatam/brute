#include "async_server.h"

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

static void *
result_receiver (void *arg)
{
  return (NULL);
}

static void *
task_sender (void *arg)
{
  return (NULL);
}

static void *
handle_clients (void *arg)
{
  serv_context_t *serv_ctx = *(serv_context_t **)arg;
  mt_context_t *mt_ctx = &serv_ctx->context;

  acl_context_t cl_ctx = {
    .context = serv_ctx,
  };

  while (true)
    {
      if ((cl_ctx.socket_fd = accept (serv_ctx->socket_fd, NULL, NULL)) == -1)
        {
          print_error ("Could not accept new connection\n");
          continue;
        }

      if (thread_create (&mt_ctx->thread_pool, task_sender, &cl_ctx,
                         sizeof (cl_ctx))
          == S_FAILURE)
        {
          print_error ("Could not create task sender thread\n");

          close_client (cl_ctx.socket_fd);
          continue;
        }

      if (thread_create (&mt_ctx->thread_pool, result_receiver, &cl_ctx,
                         sizeof (cl_ctx))
          == S_FAILURE)
        {
          print_error ("Could not create result receiver thread\n");

          close_client (cl_ctx.socket_fd);
          continue;
        }
    }

  return (NULL);
}

bool
run_async_server (task_t *task, config_t *config)
{
  serv_context_t context;
  serv_context_t *context_ptr = &context;

  if (serv_context_init (&context, config) == S_FAILURE)
    {
      print_error ("Could not initialize server context\n");
      return (false);
    }

  if (thread_create (&context.context.thread_pool, handle_clients,
                     &context_ptr, sizeof (context_ptr))
      == S_FAILURE)
    {
      print_error ("Could not create clients thread\n");
      goto fail;
    }

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  mt_context_t *mt_ctx = (mt_context_t *)&context;

  brute (task, config, queue_push_wrapper, mt_ctx);

  if (wait_password (mt_ctx) == S_FAILURE)
    goto fail;

  if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
    {
      print_error ("Could not cancel a queue\n");
      goto fail;
    }

  if (mt_ctx->password[0] != 0)
    memcpy (task->task.password, mt_ctx->password, sizeof (mt_ctx->password));

  if (serv_context_destroy (&context) == S_FAILURE)
    print_error ("Could not destroy server context\n");

  return (mt_ctx->password[0] != 0);

fail:
  if (serv_context_destroy (&context) == S_FAILURE)
    print_error ("Could not destroy server context\n");

  return (false);
}
