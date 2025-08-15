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

typedef enum read_progress_t
{
  RP_CMD,
  RP_DATA,
} read_progress_t;

typedef struct read_state_t
{
  struct iovec vec[1];
  size_t vec_sz;
  command_t cmd;
  read_progress_t rp;
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
  command_t curr_cmd;
  bool is_writing;
  pthread_mutex_t is_writing_mutex;
  write_state_t write_state;
  queue_t task_queue;
  queue_t result_queue;
  int alph_length;
  task_t read_buffer;
} client_context_t;

static void handle_read (evutil_socket_t, short, void *);

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

  ctx->done = false;

  ctx->rctr_ctx.ev_base = event_base_new ();
  if (!ctx->rctr_ctx.ev_base)
    {
      error ("Could not initialize event base");
      goto cleanup;
    }

  if (evutil_make_socket_nonblocking (ctx->client_base.socket_fd) < 0)
    {
      error ("Could not change socket to be nonblocking");
      return (S_FAILURE);
    }

  ctx->read_state.rp = RP_CMD;

  trace ("Allocated memory for event loop base");

  if (client_base_context_init (&ctx->client_base, config, NULL) == S_FAILURE)
    {
      error ("Could not initialize client base context");
      goto cleanup;
    }

  return (S_SUCCESS);

cleanup:
  queue_destroy (&ctx->rctr_ctx.jobs_queue);
  queue_destroy (&ctx->task_queue);
  queue_destroy (&ctx->result_queue);

  client_base_context_destroy (&ctx->client_base);

  return (S_FAILURE);
}

static status_t
client_context_destroy (client_context_t *ctx)
{
  trace ("Destroying client context");
  event_base_loopbreak (ctx->rctr_ctx.ev_base);

  status_t status = S_SUCCESS;

  if (event_del (ctx->read_event) == -1)
    error ("Could not delete read event");
  event_free (ctx->read_event);

  if (queue_destroy (&ctx->rctr_ctx.jobs_queue) != QS_SUCCESS)
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
  
  if (thread_pool_cancel (&ctx->thread_pool) == S_FAILURE)
    {
      error ("Could not cancel thread pool");
      status = S_FAILURE;
      goto cleanup;
    }
  
  event_base_free (ctx->rctr_ctx.ev_base);

  trace ("Waited for all threads to end, closing the connection now");

cleanup:
  queue_destroy (&ctx->task_queue);
  queue_destroy (&ctx->result_queue);

  client_base_context_destroy (&ctx->client_base);

  return (status);
}

static status_t
send_result_job (void *arg)
{
  client_context_t *ctx = arg;

  queue_status_t qs;

  result_t result;
  qs = queue_trypop (&ctx->result_queue, &result);
  if (qs == QS_EMPTY)
    {
      error ("Result queue is empty");
      return (S_SUCCESS);
    }

  trace ("Got result from a result queue");

  io_state_t *write_state_base = &ctx->write_state.base_state;
  write_state_base->vec[0].iov_base = &result;
  write_state_base->vec[0].iov_len = sizeof (result);
  write_state_base->vec_sz = 1;

  write_state_write (ctx->client_base.socket_fd, &ctx->write_state);

  if (ctx->write_state.base_state.vec_sz != 0)
    return (push_job (&ctx->rctr_ctx, ctx, send_result_job));

  trace ("Sent result %s to server", result.password);

  return (S_SUCCESS);
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

  if (queue_push_back (&ctx->result_queue, &task.result) != QS_SUCCESS)
    return (S_FAILURE);

  return (push_job (&ctx->rctr_ctx, ctx, send_result_job));
}

static status_t
read_command (client_context_t *ctx)
{
  ctx->read_state.vec[0].iov_base = &ctx->read_state.cmd;
  ctx->read_state.vec[0].iov_len = sizeof (command_t);

  size_t bytes_read
      = readv (ctx->client_base.socket_fd, ctx->read_state.vec, 1);
  if ((ssize_t)bytes_read <= 0)
    {
      error ("Could not read command from a server");
      client_context_destroy (ctx);
      return (S_FAILURE);
    }

  ctx->read_state.vec[0].iov_len -= bytes_read;
  ctx->read_state.vec[0].iov_base += bytes_read;

  if (ctx->read_state.vec[0].iov_len == 0)
    {
      ctx->read_state.rp = RP_DATA;
      return (S_SUCCESS);
    }

  return (S_FAILURE);
}

static status_t
tryread (client_context_t *ctx)
{
  size_t bytes_read
      = readv (ctx->client_base.socket_fd, ctx->read_state.vec, 1);
  if ((ssize_t)bytes_read <= 0)
    {
      client_context_destroy (ctx);
      return (S_FAILURE);
    }

  ctx->read_state.vec->iov_len -= bytes_read;
  ctx->read_state.vec->iov_base += bytes_read;

  return (ctx->read_state.vec->iov_len == 0 ? S_SUCCESS : S_FAILURE);
}

static void
handle_read (evutil_socket_t socket_fd, short what, void *arg)
{
  assert (what == EV_READ);
  /* We already have socket_fd in client_context_t */
  (void)socket_fd; /* to suppress "unused parameter" warning */

  client_context_t *ctx = arg;

  if (ctx->read_state.rp == RP_CMD && read_command (ctx) == S_FAILURE)
    return;

  switch (ctx->read_state.cmd)
    {
    case CMD_ALPH:
      trace ("Got an alphabet");
      ctx->read_state.vec[0].iov_base = &ctx->alph_length;
      ctx->read_state.vec[0].iov_len = sizeof (int);
      if (tryread (ctx) == S_FAILURE)
        {
          error ("Could not read alphabet length from a server");
          break;
        }
      ctx->read_state.vec[0].iov_base = ctx->client_base.alph;
      ctx->read_state.vec[0].iov_len = ctx->alph_length;
      if (tryread (ctx) == S_FAILURE)
        {
          error ("Could not read alph from a server");
          break;
        }
      ctx->client_base.alph[ctx->alph_length] = 0;
      trace ("Alphabet is %s", ctx->client_base.alph);
      break;
    case CMD_HASH:
      trace ("Got a hash");
      ctx->read_state.vec[0].iov_base = ctx->client_base.hash;
      ctx->read_state.vec[0].iov_len = HASH_LENGTH;
      if (tryread (ctx) == S_FAILURE)
        {
          error ("Could not read hash from a server");
          break;
        }
      trace ("Hash is %s", ctx->client_base.hash);
      break;
    case CMD_TASK:
      ctx->read_state.vec[0].iov_base = &ctx->read_buffer;
      ctx->read_state.vec[0].iov_len = sizeof (task_t);
      trace ("Got a task");
      if (tryread (ctx) == S_FAILURE)
        {
          error ("Could not read task from a server");
          return;
        }
      if (queue_push_back (&ctx->task_queue, &ctx->read_buffer) != QS_SUCCESS)
        error ("Could not push a task to the task queue");
      push_job (&ctx->rctr_ctx, ctx, process_task_job);
      break;
    default:
      error ("Unexpected read");
      break;
    }

  ctx->read_state.rp = RP_CMD;
}

bool
run_reactor_client (config_t *config)
{
  client_context_t ctx;

  if (client_context_init (&ctx, config) == S_FAILURE)
    return (false);

  if (srv_connect (&ctx.client_base) == S_FAILURE)
    goto cleanup;

  reactor_context_t *rctr_ctx_ptr = &ctx.rctr_ctx;

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
    error ("Could not destroy asynchronous client context");

  return (false);
}
