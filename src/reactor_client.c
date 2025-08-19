#include "reactor_client.h"

#include "brute.h"
#include "client_common.h"
#include "common.h"
#include "log.h"
#include "queue.h"
#include "reactor_common.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <assert.h>
#include <event2/event.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef enum read_stage_t
{
  RS_CMD,
  RS_LEN,
  RS_DATA,
} read_stage_t;

typedef struct read_state_t
{
  struct iovec vec[1];
  read_stage_t stage;
  bool is_partial;
  command_t cmd;
  int alph_len;
} read_state_t;

typedef struct client_context_t
{
  client_base_context_t client_base;
  reactor_context_t rctr_ctx;
  thread_pool_t thread_pool;
  pthread_mutex_t mutex;
  volatile bool done;
  pthread_cond_t cond_sem;
  struct event_base *ev_base;
  struct event *read_event;
  read_state_t read_state;
  bool is_writing;
  pthread_mutex_t is_writing_mutex;
  write_state_t write_state;
  queue_t task_queue;
  queue_t result_queue;
  task_t read_buffer;
  result_t write_buffer;
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
  if (queue_init (&ctx->rctr_ctx.jobs_queue, sizeof (job_t)) != QS_SUCCESS)
    {
      error ("Could not initialize jobs queue");
      return (S_FAILURE);
    }
  if (queue_init (&ctx->task_queue, sizeof (task_t)) != QS_SUCCESS)
    {
      error ("Could not initialize task queue");
      queue_destroy (&ctx->rctr_ctx.jobs_queue);
      return (S_FAILURE);
    }
  if (queue_init (&ctx->result_queue, sizeof (result_t)) != QS_SUCCESS)
    {
      error ("Could not initialize result queue");
      queue_destroy (&ctx->rctr_ctx.jobs_queue);
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
  if (pthread_mutex_init (&ctx->is_writing_mutex, NULL) != 0)
    {
      error ("Could not initialize mutex for write state");
      goto cleanup;
    }

  ctx->done = false;

  ctx->rctr_ctx.ev_base = event_base_new ();
  if (!ctx->rctr_ctx.ev_base)
    {
      error ("Could not initialize event base");
      goto cleanup;
    }

  ctx->read_state.stage = RS_CMD;
  ctx->read_state.is_partial = false;
  ctx->read_state.alph_len = -1;
  ctx->read_state.cmd = CMD_HASH;

  trace ("Allocated memory for event loop base");

  if (client_base_context_init (&ctx->client_base, config, NULL) == S_FAILURE)
    {
      error ("Could not initialize client base context");
      client_base_context_destroy (&ctx->client_base);
      goto event_base_cleanup;
    }

  return (S_SUCCESS);

event_base_cleanup:
  event_base_free (ctx->rctr_ctx.ev_base);

cleanup:
  queue_destroy (&ctx->rctr_ctx.jobs_queue);
  queue_destroy (&ctx->task_queue);
  queue_destroy (&ctx->result_queue);

  return (S_FAILURE);
}

static status_t
client_context_destroy (client_context_t *ctx)
{
  trace ("Destroying client context");
  status_t status = S_SUCCESS;

  if (queue_cancel (&ctx->rctr_ctx.jobs_queue) != QS_SUCCESS)
    {
      error ("Could not destroy jobs queue");
      status = S_FAILURE;
      goto cleanup;
    }
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

  event_base_free (ctx->rctr_ctx.ev_base);
  trace ("Freed an event base");

cleanup:
  queue_destroy (&ctx->rctr_ctx.jobs_queue);
  queue_destroy (&ctx->task_queue);
  queue_destroy (&ctx->result_queue);

  client_base_context_destroy (&ctx->client_base);

  // TODO: destroy mutexes and conditional variables

  return (status);
}

static void
result_write_state_setup (client_context_t *ctx)
{
  io_state_t *base_state = &ctx->write_state.base_state;
  base_state->vec[0].iov_base = &ctx->write_buffer;
  base_state->vec[0].iov_len = sizeof (ctx->write_buffer);
  base_state->vec_sz = 1;
}

static status_t
send_result_job (void *arg)
{
  client_context_t *ctx = arg;

  write_state_write (ctx->client_base.socket_fd, &ctx->write_state);

  if (ctx->write_state.base_state.vec_sz != 0)
    return (push_job (&ctx->rctr_ctx, ctx, send_result_job));

  trace ("Sent %s result %s to server",
         ctx->write_buffer.is_correct ? "correct" : "incorrect",
         ctx->write_buffer.password);

  queue_status_t qs = queue_trypop (&ctx->result_queue, &ctx->write_buffer);
  if (qs == QS_EMPTY)
    {
      pthread_mutex_lock (&ctx->is_writing_mutex);
      ctx->is_writing = false;
      pthread_mutex_unlock (&ctx->is_writing_mutex);

      return (S_SUCCESS);
    }
  if (qs == QS_FAILURE)
    {
      error ("Could not pop from a result queue");
      return (S_FAILURE);
    }

  result_write_state_setup (ctx);

  return (push_job (&ctx->rctr_ctx, ctx, send_result_job));
}

static status_t
process_task_job (void *arg)
{
  client_context_t *ctx = arg;

  st_context_t st_context = {
    .hash = ctx->client_base.hash,
    .data = { .initialized = 0 },
  };

  queue_status_t qs;

  task_t task;
  qs = queue_trypop (&ctx->task_queue, &task);
  if (qs == QS_EMPTY)
    {
      error ("Task queue is empty");
      return (S_SUCCESS);
    }
  if (qs == QS_FAILURE)
    {
      error ("Could not pop a task from the task queue");
      return (S_FAILURE);
    }

  trace ("Got task from task queue");

  task.result.is_correct
      = brute (&task, ctx->client_base.config, st_password_check, &st_context);
  trace ("Processed task: %s", task.result.password);

  pthread_mutex_lock (&ctx->is_writing_mutex);
  bool is_writing = ctx->is_writing;
  ctx->is_writing = true;
  pthread_mutex_unlock (&ctx->is_writing_mutex);

  // client_finish on != QS_SUCCESS?
  if (is_writing)
    return (queue_push_back (&ctx->result_queue, &task.result) == QS_SUCCESS
                ? S_SUCCESS
                : S_FAILURE);

  memcpy (&ctx->write_buffer, &task.result, sizeof (task.result));
  result_write_state_setup (ctx);

  return (push_job (&ctx->rctr_ctx, ctx, send_result_job));
}

static void
client_finish (client_context_t *ctx)
{
  if (event_del (ctx->read_event) == -1)
    error ("Could not delete read event");
  event_free (ctx->read_event);

  event_base_loopbreak (ctx->ev_base);

  ctx->done = true;
  if (pthread_cond_signal (&ctx->cond_sem) != 0)
    error ("Could not signal on a conditional semaphore");

  trace ("Sent a signal to main thread about work finishing");
}

static status_t
tryread (client_context_t *ctx, void *base, size_t len)
{
  if (!ctx->read_state.is_partial)
    {
      ctx->read_state.vec[0].iov_base = base;
      ctx->read_state.vec[0].iov_len = len;
    }

  size_t bytes_read
      = readv (ctx->client_base.socket_fd, ctx->read_state.vec, 1);
  if ((ssize_t)bytes_read <= 0)
    {
      client_finish (ctx);
      return (S_FAILURE);
    }

  ctx->read_state.vec[0].iov_len -= bytes_read;
  ctx->read_state.vec[0].iov_base += bytes_read;

  ctx->read_state.is_partial = (ctx->read_state.vec->iov_len != 0);
  return (ctx->read_state.is_partial ? S_FAILURE : S_SUCCESS);
}

static void
handle_read (evutil_socket_t socket_fd, short what, void *arg)
{
  assert (what == EV_READ);
  /* We already have socket_fd in client_context_t */
  (void)socket_fd; /* to suppress "unused parameter" warning */

  client_context_t *ctx = arg;

  switch (ctx->read_state.stage)
    {
    case RS_CMD:
      if (tryread (ctx, &ctx->read_state.cmd, sizeof (command_t)) == S_FAILURE)
        {
          error ("Could not read command");
          return;
        }
      ctx->read_state.stage
          = ctx->read_state.cmd == CMD_ALPH ? RS_LEN : RS_DATA;
      break;
    case RS_LEN:
      if (tryread (ctx, &ctx->read_state.alph_len, sizeof (int)) == S_FAILURE)
        {
          error ("Could not read alphabet length");
          return;
        }
      ctx->read_state.stage = RS_DATA;
      break;
    case RS_DATA:
      switch (ctx->read_state.cmd)
        {
        case CMD_HASH:
          if (tryread (ctx, ctx->client_base.hash, HASH_LENGTH) == S_FAILURE)
            {
              error ("Could not read hash");
              return;
            }
          trace ("Got hash: %s", ctx->client_base.hash);
          break;
        case CMD_ALPH:
          if (ctx->read_state.alph_len < 0)
            {
              error ("Alphabet length should be greater than 0");
              goto fail;
            }
          if (tryread (ctx, ctx->client_base.alph, ctx->read_state.alph_len)
              == S_FAILURE)
            {
              error ("Could not read command");
              return;
            }
          ctx->client_base.alph[ctx->read_state.alph_len] = 0;
          trace ("Got alphabet: %s", ctx->client_base.alph);
          break;
        case CMD_TASK:
          if (tryread (ctx, &ctx->read_buffer, sizeof (task_t)) == S_FAILURE)
            {
              error ("Could not read task");
              return;
            }
          if (queue_push_back (&ctx->task_queue, &ctx->read_buffer)
              != QS_SUCCESS)
            {
              error ("Could not push a task to the task queue");
              goto fail;
            }
          if (push_job (&ctx->rctr_ctx, ctx, process_task_job) == S_FAILURE)
            {
              error ("Could not push a job to the jobs queue");
              goto fail;
            }
          break;
        default:
          error ("Got unexpected command");
          goto fail;
        }

      ctx->read_state.stage = RS_CMD;
    }

  return;

fail:
  client_finish (ctx);
}

bool
run_reactor_client (config_t *config)
{
  client_context_t ctx;

  if (client_context_init (&ctx, config) == S_FAILURE)
    return (false);

  if (srv_connect (&ctx.client_base) == S_FAILURE)
    goto cleanup;

  ctx.read_event = event_new (ctx.rctr_ctx.ev_base, ctx.client_base.socket_fd,
                              EV_READ | EV_PERSIST, handle_read, &ctx);
  if (!ctx.read_event)
    {
      error ("Could not create read event");
      goto cleanup;
    }

  if (event_add (ctx.read_event, NULL) != 0)
    {
      error ("Could not add event to event base");
      goto cleanup;
    }

  reactor_context_t *rctr_ctx_ptr = &ctx.rctr_ctx;

  int number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;
  if (create_threads (&ctx.thread_pool, 1, handle_io, &rctr_ctx_ptr,
                      sizeof (rctr_ctx_ptr), "i/o handler")
      == 0)
    goto cleanup;
  trace ("Created I/O handler thread");

  if (!thread_create (&ctx.thread_pool, dispatch_event_loop, &ctx.rctr_ctx,
                      sizeof (ctx.rctr_ctx), "event loop dispatcher"))
    goto cleanup;
  trace ("Created event loop dispatcher thread");

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

  if (status != S_FAILURE)
    trace ("Got signal on conditional semaphore");

cleanup:
  if (client_context_destroy (&ctx) == S_FAILURE)
    error ("Could not destroy reactor client context");

  return (false);
}
