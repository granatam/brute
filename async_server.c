#include "async_server.h"

#include "brute.h"
#include "common.h"
#include "log.h"
#include "multi.h"
#include "server_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static acl_context_t *
acl_context_init (acl_context_t *global_ctx)
{
  acl_context_t *ctx = malloc (sizeof (acl_context_t));
  memcpy (ctx, global_ctx, sizeof (acl_context_t));

  if (queue_init (&ctx->registry_idx, sizeof (size_t)) != QS_SUCCESS)
    {
      error ("Could not initialize registry indices queue\n");
      return (NULL);
    }
  for (size_t i = 0; i < QUEUE_SIZE; ++i)
    if (queue_push (&ctx->registry_idx, &i) != QS_SUCCESS)
      {
        error ("Could not push index to registry indices queue\n");
        return (NULL);
      }

  ctx->ref_count = 2;
  if (pthread_mutex_init (&ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize mutex\n");
      return (NULL);
    }

  return ctx;
}

static void
acl_context_destroy (acl_context_t *ctx)
{
  if (queue_cancel (&ctx->registry_idx) != QS_SUCCESS)
    {
      error ("Could not cancel registry indices queue\n");
      goto cleanup;
    }

  if (queue_destroy (&ctx->registry_idx) != QS_SUCCESS)
    {
      error ("Could not destroy registry indices queue\n");
      goto cleanup;
    }

  if (pthread_mutex_destroy (&ctx->mutex) != 0)
    {
      error ("Could not destroy mutex\n");
      goto cleanup;
    }

cleanup:
  close_client (ctx->socket_fd);
  free (ctx);
}

static void
return_tasks (acl_context_t *ctx)
{
}

static void
thread_cleanup_helper (void *arg)
{
  acl_context_t *ctx = arg;

  if (pthread_mutex_lock (&ctx->mutex) != 0)
    {
      error ("Could not lock mutex\n");
      return;
    }

  if (--ctx->ref_count == 0)
    {
      error ("freed acl_context\n");
      acl_context_destroy (ctx);
    }

  if (pthread_mutex_unlock (&ctx->mutex) != 0)
    error ("Could not unlock mutex\n");
}

static void *
result_receiver (void *arg)
{
  acl_context_t *cl_ctx = *(acl_context_t **)arg;
  mt_context_t *mt_ctx = &cl_ctx->context->context;

  while (true)
    {
      result_t task;
      if (recv_wrapper (cl_ctx->socket_fd, &task, sizeof (task), 0)
          == S_FAILURE)
        {
          error ("Could not receive result from client\n");
          break;
        }
      error ("[server receiver] Received result\n");

      if (task.is_correct)
        {
          if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
            {
              error ("Could not cancel a queue\n");
              break;
            }
          memcpy (mt_ctx->password, task.password, sizeof (task.password));
          error ("[server receiver] Received correct result %s\n",
                 task.password);
        }

      if (queue_push (&cl_ctx->registry_idx, &task.id) != QS_SUCCESS)
        {
          error ("Could not return id to a queue\n");
          break;
        }

      if (serv_signal_if_found (cl_ctx->socket_fd, mt_ctx) == S_FAILURE)
        break;

      if (mt_ctx->password[0] != 0)
        {
          error ("[server receiver] After signal\n");
          break;
        }
    }

  thread_cleanup_helper (cl_ctx);

  return (NULL);
}

static void *
task_sender (void *arg)
{
  acl_context_t *cl_ctx = *(acl_context_t **)arg;
  mt_context_t *mt_ctx = &cl_ctx->context->context;

  if (send_config_data (cl_ctx->socket_fd, mt_ctx) == S_FAILURE)
    goto cleanup;

  while (true)
    {
      size_t id;
      if (queue_pop (&cl_ctx->registry_idx, &id) != QS_SUCCESS)
        break;
      error ("[server sender] After registry_idx pop\n");

      task_t *task = &cl_ctx->registry[id];

      if (queue_pop (&mt_ctx->queue, task) != QS_SUCCESS)
        break;
      error ("[server sender] After task queue pop\n");

      task->task.id = id;
      task->task.is_correct = false;
      task->to = task->from;
      task->from = 0;

      command_t cmd = CMD_TASK;
      struct iovec vec[] = {
        { .iov_base = &cmd, .iov_len = sizeof (cmd) },
        { .iov_base = task, .iov_len = sizeof (*task) },
      };

      if (send_wrapper (cl_ctx->socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
          == S_FAILURE)
        {
          error ("Could not send task to client\n");
          // TODO: status check
          queue_push (&cl_ctx->registry_idx, &id);
          break;
        }
      error ("[server sender] Sent task\n");
    }

cleanup:
  thread_cleanup_helper (cl_ctx);

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
          error ("Could not accept new connection: %s\n", strerror (errno));
          continue;
        }

      int option = 1;
      setsockopt (cl_ctx.socket_fd, SOL_SOCKET, TCP_NODELAY, &option,
                  sizeof (option));

      acl_context_t *ctx_copy = acl_context_init (&cl_ctx);
      if (!ctx_copy)
        continue;

      // FIXME: shared cl_ctx for these 2 threads
      if (thread_create (&mt_ctx->thread_pool, task_sender, &ctx_copy,
                         sizeof (ctx_copy))
          == S_FAILURE)
        {
          error ("Could not create task sender thread\n");
          close_client (ctx_copy->socket_fd);
          acl_context_destroy (ctx_copy);
          continue;
        }

      if (thread_create (&mt_ctx->thread_pool, result_receiver, &ctx_copy,
                         sizeof (ctx_copy))
          == S_FAILURE)
        {
          error ("Could not create result receiver thread\n");
          close_client (ctx_copy->socket_fd);
          acl_context_destroy (ctx_copy);
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
      error ("Could not initialize server context\n");
      return (false);
    }

  if (thread_create (&context.context.thread_pool, handle_clients,
                     &context_ptr, sizeof (context_ptr))
      == S_FAILURE)
    {
      error ("Could not create clients thread\n");
      goto fail;
    }

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  mt_context_t *mt_ctx = (mt_context_t *)&context;

  brute (task, config, queue_push_wrapper, mt_ctx);

  error ("[server] After brute\n");

  if (wait_password (mt_ctx) == S_FAILURE)
    goto fail;

  error ("[server] After wait\n");

  if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
    {
      error ("Could not cancel a queue\n");
      goto fail;
    }

  error ("Cancelled queue\n");

  if (mt_ctx->password[0] != 0)
    memcpy (task->task.password, mt_ctx->password, sizeof (mt_ctx->password));

  if (serv_context_destroy (&context) == S_FAILURE)
    error ("Could not destroy server context\n");

  error ("Destroyed context\n");

  return (mt_ctx->password[0] != 0);

fail:
  error ("Fail mark\n");

  if (serv_context_destroy (&context) == S_FAILURE)
    error ("Could not destroy server context\n");

  return (false);
}
