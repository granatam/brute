#include "async_client.h"

#include "brute.h"
#include "client_common.h"
#include "common.h"
#include "log.h"
#include "queue.h"
#include "single.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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
  client_base_context_t client_base;
  thread_pool_t thread_pool;
  queue_t task_queue;
  queue_t result_queue;
  pthread_mutex_t mutex;
  bool done;
  pthread_cond_t cond_sem;
} client_context_t;

static status_t
client_context_init (client_context_t *ctx, config_t *config)
{
  memset (ctx, 0, sizeof (*ctx));

  if (thread_pool_init (&ctx->thread_pool) == S_FAILURE)
    {
      error ("Could not initialize thread pool");
      return (S_FAILURE);
    }
  if (queue_init (&ctx->task_queue, sizeof (task_t)) != QS_SUCCESS)
    {
      error ("Could not initialize task queue");
      return (S_FAILURE);
    }
  if (queue_init (&ctx->result_queue, sizeof (result_t)) != QS_SUCCESS)
    {
      error ("Could not initialize result queue");
      queue_destroy (&ctx->task_queue);
      return (S_FAILURE);
    }
  if (pthread_mutex_init (&ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize mutex");
      goto cleanup;
    }
  if (pthread_cond_init (&ctx->cond_sem, NULL) != 0)
    {
      error ("Could not initialize conditional semaphore");
      goto cleanup;
    }

  ctx->done = false;

  if (client_base_context_init (&ctx->client_base, config, NULL) == S_FAILURE)
    {
      error ("Could not initialize client base context");
      goto cleanup;
    }

  return (S_SUCCESS);

cleanup:
  queue_destroy (&ctx->task_queue);
  queue_destroy (&ctx->result_queue);

  client_base_context_destroy (&ctx->client_base);

  return (S_FAILURE);
}

static status_t
client_context_destroy (client_context_t *ctx)
{
  status_t status = S_SUCCESS;

  if (queue_cancel (&ctx->task_queue) != QS_SUCCESS)
    {
      error ("Could not cancel task queue");
      status = S_FAILURE;
      goto cleanup;
    }
  if (queue_cancel (&ctx->result_queue) != QS_SUCCESS)
    {
      error ("Could not cancel result queue");
      status = S_FAILURE;
      goto cleanup;
    }
  if (thread_pool_join (&ctx->thread_pool) == S_FAILURE)
    {
      error ("Could not cancel thread pool");
      status = S_FAILURE;
      goto cleanup;
    }

  trace ("Waited for all threads to end, closing the connection now");

cleanup:
  queue_destroy (&ctx->task_queue);
  queue_destroy (&ctx->result_queue);

  client_base_context_destroy (&ctx->client_base);

  return (status);
}

static void *
client_worker (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;

  st_context_t st_context = {
    .hash = ctx->client_base.config->hash,
    .data = { .initialized = 0 },
  };

  while (true)
    {
      if (ctx->client_base.config->timeout > 0)
        if (ms_sleep (ctx->client_base.config->timeout) != 0)
          error ("Could not sleep");

      task_t task;
      if (queue_pop (&ctx->task_queue, &task) != QS_SUCCESS)
        return (NULL);
      trace ("Got new task to process");

      task.result.is_correct = brute (&task, ctx->client_base.config,
                                      st_password_check, &st_context);
      trace ("Processed task");

      if (queue_push (&ctx->result_queue, &task.result) != QS_SUCCESS)
        return (NULL);
      trace ("Pushed processed task to result queue");
    }
  return (NULL);
}

static status_t
handle_task (client_base_context_t *client_base, task_t *task, void *arg)
{
  (void)client_base;

  client_context_t *ctx = arg;

  if (queue_push (&ctx->task_queue, task) != QS_SUCCESS)
    {
      error ("Could not push to task queue");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

static void *
task_receiver (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;

  task_t task;
  client_base_recv_loop (&ctx->client_base, &task, handle_task, ctx);

  ctx->done = true;
  if (pthread_cond_signal (&ctx->cond_sem) != 0)
    error ("Could not signal on a conditional semaphore");

  trace ("Signaled to main thread about receiving end");

  return (NULL);
}

static void *
result_sender (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;

  result_t result;
  while (true)
    {
      if (queue_pop (&ctx->result_queue, &result) != QS_SUCCESS)
        goto end;
      trace ("Got new result from result queue");

      struct iovec vec[] = {
        { .iov_base = &result, .iov_len = sizeof (result) },
      };

      if (send_wrapper (ctx->client_base.socket_fd, vec,
                        sizeof (vec) / sizeof (vec[0]))
          == S_FAILURE)
        {
          error ("Could not send result to server");
          goto end;
        }
      trace ("Sent %s result %s to server",
             result.is_correct ? "correct" : "incorrect", result.password);
    }

end:
  ctx->done = true;

  if (pthread_cond_signal (&ctx->cond_sem) != 0)
    error ("Could not signal on a conditional semaphore");

  trace ("Sent a signal to main thread about work finishing");

  return (NULL);
}

bool
run_async_client (config_t *config)
{
  client_context_t ctx;

  if (client_context_init (&ctx, config) == S_FAILURE)
    return (false);

  if (srv_connect (&ctx.client_base) == S_FAILURE)
    goto cleanup;

  client_context_t *ctx_ptr = &ctx;

  if (!thread_create (&ctx.thread_pool, task_receiver, &ctx_ptr,
                      sizeof (ctx_ptr), "async receiver"))
    {
      error ("Could not create receiver thread");
      goto cleanup;
    }
  trace ("Created receiver thread");

  if (!thread_create (&ctx.thread_pool, result_sender, &ctx_ptr,
                      sizeof (ctx_ptr), "async sender"))
    {
      error ("Could not create sender thread");
      goto cleanup;
    }
  trace ("Created sender thread");

  if (create_threads (&ctx.thread_pool, config->number_of_threads,
                      client_worker, &ctx_ptr, sizeof (ctx_ptr),
                      "async worker")
      == 0)
    goto cleanup;
  trace ("Created worker thread");

  if (pthread_mutex_lock (&ctx.mutex) != 0)
    {
      error ("Could not lock a mutex");
      goto cleanup;
    }
  status_t status = S_SUCCESS;

  while (!ctx.done)
    if (pthread_cond_wait (&ctx.cond_sem, &ctx.mutex) != 0)
      {
        error ("Could not wait on a condition");
        status = S_FAILURE;
        break;
      }

  if (pthread_mutex_unlock (&ctx.mutex) != 0)
    {
      error ("Could not unlock a mutex");
      goto cleanup;
    }

  if (S_FAILURE == status)
    goto cleanup;

  trace ("Got signal on conditional semaphore");

cleanup:
  if (client_context_destroy (&ctx) == S_FAILURE)
    error ("Could not destroy asynchronous client context");

  return (false);
}
