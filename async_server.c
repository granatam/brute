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
  acl_context_t *cl_ctx = (acl_context_t *)arg;
  mt_context_t *mt_ctx = &cl_ctx->context->context;

  while (true)
    {
      result_t task;
      if (recv_wrapper (cl_ctx->socket_fd, &task, sizeof (task), 0)
          == S_FAILURE)
        {
          print_error ("Could not receive result from client\n");
          return (NULL);
        }

      if (task.is_correct)
        {
          if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
            {
              print_error ("Could not cancel a queue\n");
              return (NULL);
            }
          memcpy (mt_ctx->password, task.password, sizeof (task.password));
        }

      if (queue_push (&cl_ctx->registry_idx, &task.id) != QS_SUCCESS)
        {
          print_error ("Could not return id to a queue\n");
          return (NULL);
        }

      if (serv_signal_if_found (cl_ctx->socket_fd, mt_ctx) == S_FAILURE)
        return (NULL);
    }

  return (NULL);
}

static void *
task_sender (void *arg)
{
  acl_context_t *cl_ctx = (acl_context_t *)arg;
  mt_context_t *mt_ctx = &cl_ctx->context->context;

  if (send_hash (cl_ctx->socket_fd, mt_ctx) == S_FAILURE)
    return (NULL);

  if (send_alph (cl_ctx->socket_fd, mt_ctx) == S_FAILURE)
    return (NULL);

  while (true)
    {
      size_t id;
      if (queue_pop (&cl_ctx->registry_idx, &id) != QS_SUCCESS)
        return (NULL);

      task_t *task = &cl_ctx->registry[id];

      if (queue_pop (&mt_ctx->queue, &task) != QS_SUCCESS)
        return (NULL);

      task->task.id = id;
      task->to = task->from;
      task->from = 0;

      command_t cmd = CMD_TASK;
      if (send_wrapper (cl_ctx->socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
        {
          print_error ("Could not send CMD_TASK to client\n");
          // TODO: status check
          queue_push (&cl_ctx->registry_idx, &id);

          return (NULL);
        }

      if (send_wrapper (cl_ctx->socket_fd, task, sizeof (*task), 0)
          == S_FAILURE)
        {
          print_error ("Could not send task to client\n");
          // TODO: status check
          queue_push (&cl_ctx->registry_idx, &id);

          return (NULL);
        }
    }

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

      // TODO: status checks
      queue_init (&cl_ctx.registry_idx, sizeof (size_t));
      for (size_t i = 0; i < QUEUE_SIZE; ++i)
        queue_push (&cl_ctx.registry_idx, &i);

      acl_context_t *cl_ctx_copy = malloc (sizeof (acl_context_t));
      if (!cl_ctx_copy)
        {
          print_error ("Could not allocate memory for client context copy\n");

          close_client (cl_ctx.socket_fd);
          continue;
        }
      cl_ctx_copy = *(acl_context_t **)&cl_ctx;

      // FIXME: shared cl_ctx for these 2 threads
      if (thread_create (&mt_ctx->thread_pool, task_sender, cl_ctx_copy,
                         sizeof (cl_ctx_copy))
          == S_FAILURE)
        {
          print_error ("Could not create task sender thread\n");

          close_client (cl_ctx.socket_fd);
          continue;
        }

      if (thread_create (&mt_ctx->thread_pool, result_receiver, cl_ctx_copy,
                         sizeof (cl_ctx_copy))
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
