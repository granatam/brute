#include "async_server.h"

#include "brute.h"
#include "log.h"
#include "multi.h"
#include "server_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static acl_context_t *
acl_context_init (serv_context_t *global_ctx)
{
  acl_context_t *ctx = calloc (1, sizeof (*ctx));
  if (!ctx)
    {
      error ("Could not allocate memory for client context");
      return (NULL);
    }
  ctx->context = global_ctx;

  if (queue_init (&ctx->registry_idx, sizeof (int)) != QS_SUCCESS)
    {
      error ("Could not initialize registry indices queue");
      goto cleanup;
    }
  for (int i = 0; i < QUEUE_SIZE; ++i)
    if (queue_push (&ctx->registry_idx, &i) != QS_SUCCESS)
      {
        error ("Could not push index to registry indices queue");
        goto cleanup;
      }

  ctx->ref_count = 2;
  if (pthread_mutex_init (&ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize mutex");
      goto cleanup;
    }

  return ctx;

cleanup:
  if (queue_cancel (&ctx->registry_idx) != QS_SUCCESS)
    error ("Could not cancel registry indices queue");

  if (queue_destroy (&ctx->registry_idx) != QS_SUCCESS)
    error ("Could not destroy registry indices queue");

  return NULL;
}

static void
acl_context_destroy (acl_context_t *ctx)
{
  trace ("Destroying client context");

  if (queue_cancel (&ctx->registry_idx) != QS_SUCCESS)
    error ("Could not cancel registry indices queue");

  trace ("Cancelled registry indices queue");

  if (queue_destroy (&ctx->registry_idx) != QS_SUCCESS)
    {
      error ("Could not destroy registry indices queue");
      goto cleanup;
    }

  trace ("Destroyed registry indices queue");

  if (pthread_mutex_destroy (&ctx->mutex) != 0)
    {
      error ("Could not destroy mutex");
      goto cleanup;
    }

cleanup:
  close_client (ctx->socket_fd);
  free (ctx);
  ctx = NULL;
}

static status_t
return_tasks (acl_context_t *ctx)
{
  mt_context_t *mt_ctx = &ctx->context->context;
  status_t status = S_SUCCESS;

  for (int i = 0; i < QUEUE_SIZE; ++i)
    {
      if (ctx->registry_used[i])
        {
          if (queue_push_back (&mt_ctx->queue, &ctx->registry[i])
              != QS_SUCCESS)
            {
              status = S_FAILURE;
              break;
            }
          ctx->registry_used[i] = false;
        }
    }

  return (status);
}

static void
sender_receiver_cleanup (void *arg)
{
  acl_context_t *ctx = arg;

  if (pthread_mutex_lock (&ctx->mutex) != 0)
    return;
  pthread_cleanup_push (cleanup_mutex_unlock, &ctx->mutex);

  return_tasks (ctx);

  if (--ctx->ref_count == 0)
    acl_context_destroy (ctx);

  pthread_cleanup_pop (!0);
}

static void *
result_receiver (void *arg)
{
  acl_context_t *cl_ctx = *(acl_context_t **)arg;
  mt_context_t *mt_ctx = &cl_ctx->context->context;

  pthread_cleanup_push (sender_receiver_cleanup, cl_ctx);
  while (true)
    {
      result_t task;
      if (recv_wrapper (cl_ctx->socket_fd, &task, sizeof (task), 0)
          == S_FAILURE)
        {
          error ("Could not receive result from client");
          break;
        }

      if (task.is_correct)
        {
          if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
            {
              error ("Could not cancel a queue");
              break;
            }
          memcpy (mt_ctx->password, task.password, sizeof (task.password));
        }

      trace ("Received %s result %s from client",
             task.is_correct ? "correct" : "incorrect", task.password);

      if (queue_push (&cl_ctx->registry_idx, &task.id) != QS_SUCCESS)
        {
          error ("Could not return id to a queue");
          break;
        }

      trace ("Pushed index of received task back to indices queue");

      cl_ctx->registry_used[task.id] = false;

      if (serv_signal_if_found (mt_ctx) == S_FAILURE)
        break;

      if (mt_ctx->password[0] != 0)
        break;
    }

  trace ("Cleaning up receiver thread");
  pthread_cleanup_pop (!0);

  return (NULL);
}

static void *
task_sender (void *arg)
{
  acl_context_t *cl_ctx = *(acl_context_t **)arg;
  mt_context_t *mt_ctx = &cl_ctx->context->context;

  pthread_cleanup_push (sender_receiver_cleanup, cl_ctx);
  if (send_config_data (cl_ctx->socket_fd, mt_ctx) == S_FAILURE)
    goto cleanup;

  while (true)
    {
      int id;
      if (queue_pop (&cl_ctx->registry_idx, &id) != QS_SUCCESS)
        break;

      trace ("Got index from registry");

      task_t *task = &cl_ctx->registry[id];
      if (queue_pop (&mt_ctx->queue, task) != QS_SUCCESS)
        {
          if (queue_push (&cl_ctx->registry_idx, &id) != QS_SUCCESS)
            error ("Could not push back id to registry indices queue");
          break;
        }

      trace ("Got task from global queue");

      cl_ctx->registry_used[id] = true;

      task_t task_copy = *task;
      task_copy.task.id = id;
      task_copy.to = task->from;
      task_copy.from = 0;

      if (send_task (cl_ctx->socket_fd, &task_copy) == S_FAILURE)
        {
          error ("Could not send task to client");
          if (queue_push_back (&mt_ctx->queue, task) != QS_SUCCESS)
            error ("Could not push back task to global queue");
          cl_ctx->registry_used[task->task.id] = false;
          break;
        }
    }

cleanup:
  trace ("Cleaning up sender thread");
  pthread_cleanup_pop (!0);

  return (NULL);
}

static void
accepter_cleanup (void *arg)
{
  if (!arg)
    return;

  acl_context_t *ctx = arg;
  acl_context_destroy (ctx);
}

static void *
handle_clients (void *arg)
{
  serv_context_t *srv_ctx = *(serv_context_t **)arg;
  mt_context_t *mt_ctx = &srv_ctx->context;

  acl_context_t *helper_ctx = NULL;
  pthread_cleanup_push (accepter_cleanup, helper_ctx);
  while (true)
    {
      acl_context_t *acl_ctx = acl_context_init (srv_ctx);
      if (!acl_ctx)
        break;
      helper_ctx = acl_ctx;

      while (true)
        {
          if (srv_ctx->socket_fd < 0)
            {
              error ("Invalid server socket");
              acl_context_destroy (acl_ctx);
              return (NULL);
            }

          acl_ctx->socket_fd = accept (srv_ctx->socket_fd, NULL, NULL);
          if (acl_ctx->socket_fd == -1)
            {
              error ("Could not accept new connection: %s", strerror (errno));
              if (errno == EINVAL)
                {
                  acl_context_destroy (acl_ctx);
                  return (NULL);
                }
              continue;
            }
          break;
        }

      trace ("Accepted new connection");

      int option = 1;
      setsockopt (acl_ctx->socket_fd, SOL_SOCKET, TCP_NODELAY, &option,
                  sizeof (option));

      trace ("Copied client context");

      pthread_t sender;
      if (!(sender
            = thread_create (&mt_ctx->thread_pool, task_sender, &acl_ctx,
                             sizeof (acl_ctx), "async sender")))
        {
          error ("Could not create task sender thread");
          acl_context_destroy (acl_ctx);
          continue;
        }

      trace ("Created a sender thread: %08x",
             mt_ctx->thread_pool.threads.prev->thread);

      if (!thread_create (&mt_ctx->thread_pool, result_receiver, &acl_ctx,
                          sizeof (acl_ctx), "async receiver"))
        {
          error ("Could not create result receiver thread");
          --acl_ctx->ref_count;
          pthread_cancel (sender);
          continue;
        }

      trace ("Created a receiver thread: %08x",
             mt_ctx->thread_pool.threads.prev->thread);
    }
  pthread_cleanup_pop (!0);

  return (NULL);
}

bool
run_async_server (task_t *task, config_t *config)
{
  signal (SIGPIPE, SIG_IGN);
  serv_context_t context;
  serv_context_t *context_ptr = &context;

  if (serv_context_init (&context, config) == S_FAILURE)
    {
      error ("Could not initialize server context");
      return (false);
    }

  if (!thread_create (&context.context.thread_pool, handle_clients,
                      &context_ptr, sizeof (context_ptr), "async accepter"))
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
