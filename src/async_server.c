#include "async_server.h"

#include "log.h"
#include "multi.h"
#include "queue.h"
#include "server_common.h"
#include "thread_pool.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <unistd.h>

typedef struct client_context_t
{
  srv_base_context_t *srv_base;
  task_t registry[QUEUE_SIZE];
  queue_t registry_idx;
  bool registry_used[QUEUE_SIZE];
  bool tasks_returned;
  int socket_fd;
  _Atomic unsigned char ref_count;
  pthread_mutex_t mutex;
} client_context_t;

static client_context_t *
client_context_init (srv_base_context_t *srv_base)
{
  client_context_t *ctx = calloc (1, sizeof (*ctx));
  if (!ctx)
    {
      error ("Could not allocate memory for client context");
      return (NULL);
    }
  trace ("Allocated memory for client context");

  ctx->srv_base = srv_base;

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

  ctx->ref_count = 1;
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

  free (ctx);

  return (NULL);
}

static void
client_context_destroy (client_context_t *ctx)
{
  if (queue_cancel (&ctx->registry_idx) != QS_SUCCESS)
    error ("Could not cancel registry indices queue");

  trace ("Cancelled registry indices queue");

  if (queue_destroy (&ctx->registry_idx) != QS_SUCCESS)
    error ("Could not destroy registry indices queue");

  trace ("Destroyed registry indices queue");

  if (pthread_mutex_destroy (&ctx->mutex) != 0)
    error ("Could not destroy mutex");

  close (ctx->socket_fd);

  free (ctx);

  trace ("Freed client context");
}

static void
sender_receiver_cleanup (void *arg)
{
  client_context_t *ctx = arg;

  /* Unblock the other thread if it's stuck on a read from the registry
   * indices queue. */
  if (queue_cancel (&ctx->registry_idx) != QS_SUCCESS)
    error ("Could not cancel registry indices queue");

  task_t to_return[QUEUE_SIZE];
  int to_return_count = 0;

  if (pthread_mutex_lock (&ctx->mutex) != 0)
    {
      error ("Could not lock mutex");
      return;
    }
  if (!ctx->tasks_returned)
    {
      ctx->tasks_returned = true;
      for (int i = 0; i < QUEUE_SIZE; ++i)
        {
          if (ctx->registry_used[i])
            {
              to_return[to_return_count++] = ctx->registry[i];
              ctx->registry_used[i] = false;
            }
        }
    }
  if (pthread_mutex_unlock (&ctx->mutex) != 0)
    {
      error ("Could not unlock mutex");
      return;
    }

  for (int i = 0; i < to_return_count; ++i)
    {
      mt_context_t *mt_ctx = (mt_context_t *)ctx;
      if (queue_push_back (&mt_ctx->queue, &to_return[i]) != QS_SUCCESS)
        {
          error ("Could not push task back to global queue");
        }
    }

  bool is_last
      = (atomic_fetch_sub_explicit (&ctx->ref_count, 1, memory_order_acq_rel)
         == 1);

  /* If sender is the first thread to exit, shutdown socket connection so the
   * receiver blocked in recv unblocks and exits. */
  if (!is_last && ctx->socket_fd >= 0)
    shutdown (ctx->socket_fd, SHUT_RDWR);

  if (is_last)
    client_context_destroy (ctx);
}

static void *
result_receiver (void *arg)
{
  client_context_t *client_ctx = *(client_context_t **)arg;
  mt_context_t *mt_ctx = &client_ctx->srv_base->mt_ctx;

  pthread_cleanup_push (sender_receiver_cleanup, client_ctx);
  while (true)
    {
      result_t task;
      io_status_t recv_status
          = recv_wrapper (client_ctx->socket_fd, &task, sizeof (task), 0);
      if (recv_status != IOS_SUCCESS)
        {
          if (recv_status == IOS_FAILURE)
            error ("Could not receive result from client");
          break;
        }

      trace ("Received %s result %s from client",
             task.is_correct ? "correct" : "incorrect", task.password);

      if (queue_push (&client_ctx->registry_idx, &task.id) != QS_SUCCESS)
        {
          error ("Could not return id to a queue");
          break;
        }

      trace ("Pushed index of received task back to indices queue");

      client_ctx->registry_used[task.id] = false;

      if (task.is_correct)
        {
          if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
            {
              error ("Could not cancel a queue");
              break;
            }
          memcpy (mt_ctx->password, task.password, sizeof (task.password));
        }

      if (srv_trysignal (mt_ctx) == S_FAILURE)
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
  client_context_t *client_ctx = *(client_context_t **)arg;
  mt_context_t *mt_ctx = &client_ctx->srv_base->mt_ctx;

  pthread_cleanup_push (sender_receiver_cleanup, client_ctx);
  if (send_config_data (client_ctx->socket_fd, mt_ctx) == S_FAILURE)
    goto cleanup;

  while (true)
    {
      int id;
      if (queue_pop (&client_ctx->registry_idx, &id) != QS_SUCCESS)
        {
          error ("Could not pop id from registry indices queue");
          break;
        }

      trace ("Got index from registry");

      task_t *task = &client_ctx->registry[id];
      if (queue_pop (&mt_ctx->queue, task) != QS_SUCCESS)
        {
          if (queue_push (&client_ctx->registry_idx, &id) != QS_SUCCESS)
            error ("Could not push back id to registry indices queue");
          break;
        }

      trace ("Got task from global queue");

      client_ctx->registry_used[id] = true;

      task_t task_copy = *task;
      task_copy.result.id = id;
      task_copy.to = task->from;
      task_copy.from = 0;

      if (send_task (client_ctx->socket_fd, &task_copy) == S_FAILURE)
        {
          error ("Could not send task to client");
          if (queue_push_back (&mt_ctx->queue, task) != QS_SUCCESS)
            error ("Could not push back task to global queue");
          client_ctx->registry_used[task->result.id] = false;
          break;
        }
    }

cleanup:
  trace ("Cleaning up sender thread");
  pthread_cleanup_pop (!0);

  return (NULL);
}

static void *
handle_clients (void *arg)
{
  srv_base_context_t *srv_base = *(srv_base_context_t **)arg;
  mt_context_t *mt_ctx = &srv_base->mt_ctx;

  client_context_t *client_ctx = NULL;
  int socket_fd = 0;
  while (true)
    {
      if (accept_client (srv_base->listen_fd, &socket_fd) == S_FAILURE)
        {
          if (srv_base->listen_fd < 0)
            break;
          error ("Could not accept client");
          continue;
        }

      if (!client_ctx)
        {
          client_ctx = client_context_init (srv_base);

          if (!client_ctx)
            break;

          client_ctx->socket_fd = socket_fd;
        }

      atomic_fetch_add_explicit (&client_ctx->ref_count, 1,
                                 memory_order_relaxed);

      pthread_t sender
          = thread_create (&mt_ctx->thread_pool, task_sender, &client_ctx,
                           sizeof (client_ctx), "async sender");
      if (!sender)
        {
          error ("Could not create task sender thread");
          goto cleanup;
        }

      trace ("Created a sender thread: %08x", sender);

      atomic_fetch_add_explicit (&client_ctx->ref_count, 1,
                                 memory_order_relaxed);

      pthread_t receiver
          = thread_create (&mt_ctx->thread_pool, result_receiver, &client_ctx,
                           sizeof (client_ctx), "async receiver");
      if (!receiver)
        {
          error ("Could not create result receiver thread");
          goto cancel_sender;
        }

      trace ("Created a receiver thread: %08x", receiver);

      if (atomic_fetch_sub_explicit (&client_ctx->ref_count, 1,
                                     memory_order_acq_rel)
          == 1)
        client_context_destroy (client_ctx);

      client_ctx = NULL;
      socket_fd = 0;

      continue;

    cancel_sender:
      pthread_cancel (sender);
      pthread_join (sender, NULL);

    cleanup:
      client_context_destroy (client_ctx);
      client_ctx = NULL;
      break;
    }

  return (NULL);
}

bool
run_async_server (task_t *task, config_t *config)
{
  signal (SIGPIPE, SIG_IGN);
  srv_base_context_t srv_base;
  srv_base_context_t *base_ptr = &srv_base;

  if (srv_base_context_init (base_ptr, config) == S_FAILURE)
    {
      error ("Could not initialize server context");
      return (false);
    }

  if (!thread_create (&srv_base.mt_ctx.thread_pool, handle_clients, &base_ptr,
                      sizeof (base_ptr), "async accepter"))
    {
      error ("Could not create clients thread");
      goto fail;
    }

  mt_context_t *mt_ctx = (mt_context_t *)base_ptr;

  if (process_tasks (task, config, mt_ctx) == S_FAILURE)
    goto fail;

  if (srv_base_context_destroy (base_ptr) == S_FAILURE)
    error ("Could not destroy server context");

  trace ("Destroyed server context");

  return (mt_ctx->password[0] != 0);

fail:
  trace ("Failed, destroying server context");

  if (srv_base_context_destroy (base_ptr) == S_FAILURE)
    error ("Could not destroy server context");

  return (false);
}
