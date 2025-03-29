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
  int socket_fd;
  thread_pool_t thread_pool;
  queue_t task_queue;
  queue_t result_queue;
  pthread_mutex_t mutex;
  config_t *config;
  bool done;
  pthread_cond_t cond_sem;
  char hash[HASH_LENGTH];
  char alph[MAX_ALPH_LENGTH];
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
  ctx->config = config;
  ctx->done = false;
  ctx->config->hash = ctx->hash;
  ctx->config->alph = ctx->alph;

  ctx->socket_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (ctx->socket_fd == -1)
    {
      error ("Could not initialize client socket");
      goto cleanup;
    }

  int option = 1;
  if (setsockopt (ctx->socket_fd, SOL_SOCKET, SO_KEEPALIVE, &option,
                  sizeof (option))
      == -1)
    {
      error ("Could not set socket option");
      goto cleanup;
    }

  if (setsockopt (ctx->socket_fd, IPPROTO_TCP, TCP_NODELAY, &option,
                  sizeof (option))
      == -1)
    {
      error ("Could not set socket option");
      goto cleanup;
    }

  return (S_SUCCESS);

cleanup:
  queue_destroy (&ctx->task_queue);
  queue_destroy (&ctx->result_queue);

  shutdown (ctx->socket_fd, SHUT_RDWR);
  if (close (ctx->socket_fd) != 0)
    {
      error ("Could not close socket");
    }
  return (S_FAILURE);
}

static void *
client_worker (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;

  st_context_t st_context = {
    .hash = ctx->config->hash,
    .data = { .initialized = 0 },
  };

  while (true)
    {
      if (ctx->config->timeout > 0)
        if (ms_sleep (ctx->config->timeout) != 0)
          {
            error ("Could not sleep");
          }

      task_t task;
      if (queue_pop (&ctx->task_queue, &task) != QS_SUCCESS)
        return (NULL);
      trace ("Got new task to process");

      task.task.is_correct
          = brute (&task, ctx->config, st_password_check, &st_context);
      trace ("Processed task");

      if (queue_push (&ctx->result_queue, &task.task) != QS_SUCCESS)
        return (NULL);
      trace ("Pushed processed task to result queue");
    }
  return (NULL);
}

static void *
task_receiver (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;

  task_t task;
  while (true)
    {
      command_t cmd;
      if (recv_wrapper (ctx->socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
        {
          error ("Could not receive command from server");
          goto end;
        }

      switch (cmd)
        {
        case CMD_ALPH:
          if (handle_alph (ctx->socket_fd, ctx->alph) == S_FAILURE)
            {
              error ("Could not handle alphabet");
              goto end;
            }
          trace ("Received alphabet %s from server", ctx->alph);
          break;
        case CMD_HASH:
          if (handle_hash (ctx->socket_fd, ctx->hash) == S_FAILURE)
            {
              error ("Could not handle hash");
              goto end;
            }
          trace ("Received hash %s from server", ctx->hash);
          break;
        case CMD_TASK:
          if (recv_wrapper (ctx->socket_fd, &task, sizeof (task_t), 0)
              == S_FAILURE)
            {
              error ("Could not receive task from server");
              goto end;
            }
          trace ("Received task from server");
          if (queue_push (&ctx->task_queue, &task) != QS_SUCCESS)
            goto end;
          trace ("Pushed received task to queue");
          break;
        }
    }

end:
  trace ("Disconnected from server, not receiving anything from now");
  ctx->done = true;
  if (pthread_cond_signal (&ctx->cond_sem) != 0)
    {
      error ("Could not signal on a conditional semaphore");
    }

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

      if (send_wrapper (ctx->socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
          == S_FAILURE)
        {
          error ("Could not send result to server");
          goto end;
        }
      trace ("Sent %s result %s to server",
             result.is_correct ? "correct" : "incorrect", result.password);
    }

end:
  trace ("Disconnected from server, not receiving anything from now");
  ctx->done = true;
  if (pthread_cond_signal (&ctx->cond_sem) != 0)
    error ("Could not signal on a conditional semaphore");

  trace ("Signaled to main thread about receiving end");

  return (NULL);
}

bool
run_async_client (config_t *config)
{
  client_context_t ctx;

  if (client_context_init (&ctx, config) == S_FAILURE)
    return (false);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr (config->addr);
  addr.sin_port = htons (config->port);

  if (connect (ctx.socket_fd, (struct sockaddr *)&addr, sizeof (addr)) == -1)
    {
      error ("Could not connect to server");
      goto cleanup;
    }

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
  volatile status_t status = S_SUCCESS;
  pthread_cleanup_push (cleanup_mutex_unlock, &ctx.mutex);

  while (!ctx.done)
    if (pthread_cond_wait (&ctx.cond_sem, &ctx.mutex) != 0)
      {
        error ("Could not wait on a condition");
        status = S_FAILURE;
        break;
      }

  pthread_cleanup_pop (!0);

  if (S_FAILURE == status)
    goto cleanup;

  trace ("Got signal on conditional semaphore");

  if (queue_cancel (&ctx.task_queue) != QS_SUCCESS)
    {
      error ("Could not cancel task queue");
      goto cleanup;
    }
  if (queue_cancel (&ctx.result_queue) != QS_SUCCESS)
    {
      error ("Could not cancel result queue");
      goto cleanup;
    }
  if (thread_pool_join (&ctx.thread_pool) == S_FAILURE)
    {
      error ("Could not cancel thread pool");
      goto cleanup;
    }

  trace ("Waited for all threads to end, closing the connection now");

cleanup:
  queue_destroy (&ctx.task_queue);
  queue_destroy (&ctx.result_queue);

  shutdown (ctx.socket_fd, SHUT_RDWR);
  if (close (ctx.socket_fd) != 0)
    {
      error ("Could not close socket");
    }

  return (false);
}
