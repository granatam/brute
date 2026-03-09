#include "load_client.h"
#include "priority_queue.h"

#include "client_common.h"
#include "common.h"
#include "log.h"
#include "queue.h"
#include "reactor_common.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <assert.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MS_IN_SEC (1000L)
#define USEC_IN_MSEC (1000L)
#define USEC_IN_SEC (1000000L)

#define MIN_DELAY_USEC (1000)
#define TASK_TIMEOUT_MS_MIN (100)
#define TASK_TIMEOUT_MS_MAX (5000)
#define PENDING_TASKS_QUEUE_CAP (256)

typedef struct spawner_context_t
{
  reactor_context_t *rctx;
  thread_pool_t thread_pool;
  config_t *config;
  priority_queue_t pending_tasks;
  struct event *timer_event;
  pthread_mutex_t mutex;
  pthread_cond_t cond_sem;
  volatile bool done;
} spawner_context_t;

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
  spawner_context_t *s_ctx;
  reactor_conn_t rctr_conn;
  result_t result_buffer;
  queue_t result_queue;
  pthread_mutex_t mutex;
  volatile bool done;
  pthread_cond_t cond_sem;
  read_state_t read_state;
  write_state_t write_state;
  task_t read_buffer;
  unsigned int rand_seed;
} client_context_t;

typedef struct pending_task_t
{
  struct timeval deadline;
  client_context_t *ctx;
  task_t result;
} pending_task_t;

static int
pending_task_cmp (const void *lhs, const void *rhs)
{
  const pending_task_t *a = lhs;
  const pending_task_t *b = rhs;
  return evutil_timercmp (&a->deadline, &b->deadline, <)   ? -1
         : evutil_timercmp (&a->deadline, &b->deadline, >) ? 1
                                                           : 0;
}

static void timer_callback (evutil_socket_t fd, short what, void *arg);
static void client_context_destroy (void *arg);
static void handle_read (evutil_socket_t socket_fd, short what, void *arg);

static void
spawner_context_destroy (spawner_context_t *ctx)
{
  if (queue_cancel (&ctx->rctx->jobs_queue) != QS_SUCCESS)
    error ("Could not cancel jobs queue");

  if (thread_pool_join (&ctx->thread_pool) == S_FAILURE)
    error ("Could not join thread pool");

  reactor_cleanup_clients (ctx->rctx, handle_read, client_context_destroy);

  if (event_del (ctx->timer_event) == -1)
    error ("Could not delete timer event");
  event_free (ctx->timer_event);

  priority_queue_destroy (&ctx->pending_tasks);

  if (reactor_context_destroy (ctx->rctx) != S_SUCCESS)
    error ("Could not destroy reactor context");

  pthread_mutex_destroy (&ctx->mutex);
  pthread_cond_destroy (&ctx->cond_sem);
}

static status_t
spawner_context_init (spawner_context_t *ctx, reactor_context_t *rctx,
                      config_t *config)
{
  ctx->config = config;
  if (thread_pool_init (&ctx->thread_pool) == S_FAILURE)
    {
      error ("Could not initialize thread pool");
      return (S_FAILURE);
    }
  if (reactor_context_init (rctx) != S_SUCCESS)
    {
      error ("Could not initialize reactor context");
      return (S_FAILURE);
    }
  ctx->rctx = rctx;

  if (priority_queue_init (&ctx->pending_tasks, PENDING_TASKS_QUEUE_CAP,
                           sizeof (pending_task_t), pending_task_cmp)
      != S_SUCCESS)
    {
      error ("Could not initialize pending tasks queue");
      goto cleanup;
    }

  ctx->timer_event
      = event_new (ctx->rctx->ev_base, -1, EV_TIMEOUT, timer_callback, ctx);
  if (!ctx->timer_event)
    {
      error ("Could not create timer event");
      goto cleanup;
    }

  if (pthread_mutex_init (&ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize spawner mutex");
      goto cleanup_timer;
    }
  if (pthread_cond_init (&ctx->cond_sem, NULL) != 0)
    {
      error ("Could not initialize spawner conditional semaphore");
      pthread_mutex_destroy (&ctx->mutex);
      goto cleanup_timer;
    }

  ctx->done = false;

  return (S_SUCCESS);

cleanup_timer:
  event_free (ctx->timer_event);
  priority_queue_destroy (&ctx->pending_tasks);

cleanup:
  reactor_context_destroy (rctx);
  return (S_FAILURE);
}

static status_t
client_context_init (client_context_t *ctx, spawner_context_t *s_ctx)
{
  ctx->s_ctx = s_ctx;

  if (pthread_mutex_init (&ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize mutex");
      return (S_FAILURE);
    }
  if (pthread_cond_init (&ctx->cond_sem, NULL) != 0)
    {
      error ("Could not initialize conditional semaphore");
      return (S_FAILURE);
    }

  if (queue_init (&ctx->result_queue, sizeof (result_t)) != QS_SUCCESS)
    {
      error ("Could not initialize result queue");
      return (S_FAILURE);
    }

  ctx->done = false;
  ctx->read_state.stage = RS_CMD;
  ctx->read_state.is_partial = false;
  ctx->read_state.alph_len = -1;
  ctx->read_state.cmd = CMD_HASH;
  ctx->rand_seed = (unsigned)time (NULL) ^ (unsigned)(uintptr_t)ctx;

  if (client_base_context_init (&ctx->client_base, s_ctx->config, NULL)
      == S_FAILURE)
    {
      error ("Could not initialize client base context");
      client_base_context_destroy (&ctx->client_base);
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

static void
client_context_destroy (void *arg)
{
  client_context_t *ctx = arg;

  trace ("Destroying client context");
  if (reactor_conn_destroy (&ctx->rctr_conn, ctx->client_base.socket_fd)
      != S_SUCCESS)
    {
      error ("Could not destroy reactor connection");
    }

  if (client_base_context_destroy (&ctx->client_base) != S_SUCCESS)
    {
      error ("Could not destroy client base context");
    }

  if (pthread_mutex_destroy (&ctx->mutex) != 0)
    {
      error ("Could not destroy client mutex");
    }
  if (pthread_cond_destroy (&ctx->cond_sem) != 0)
    {
      error ("Could not destroy client conditional semaphore");
    }
  if (queue_destroy (&ctx->result_queue) != QS_SUCCESS)
    {
      error ("Could not destroy result queue");
    }

  free (ctx);
}

static void
result_write_state_setup (client_context_t *ctx)
{
  io_state_t *base_state = &ctx->write_state.base_state;
  base_state->vec[0].iov_base = &ctx->result_buffer;
  base_state->vec[0].iov_len = sizeof (ctx->result_buffer);
  base_state->vec_sz = 1;
}

static status_t
send_result_job (void *arg)
{
  client_context_t *ctx = arg;

  write_state_write (ctx->client_base.socket_fd, &ctx->write_state);

  if (ctx->write_state.base_state.vec_sz != 0)
    return (push_job (ctx->rctr_conn.rctr_ctx, ctx, send_result_job));

  trace ("Sent result %s to server", ctx->result_buffer.password);

  queue_status_t qs = queue_trypop (&ctx->result_queue, &ctx->result_buffer);
  if (qs == QS_EMPTY)
    {
      pthread_mutex_lock (&ctx->rctr_conn.is_writing_mutex);
      ctx->rctr_conn.is_writing = false;
      pthread_mutex_unlock (&ctx->rctr_conn.is_writing_mutex);

      return (S_SUCCESS);
    }
  if (qs == QS_FAILURE)
    {
      error ("Could not pop from a result queue");
      return (S_FAILURE);
    }

  result_write_state_setup (ctx);

  return (push_job (ctx->rctr_conn.rctr_ctx, ctx, send_result_job));
}

static void
client_finish (client_context_t *ctx)
{
  spawner_context_t *s_ctx = ctx->s_ctx;

  if (pthread_mutex_lock (&s_ctx->mutex) != 0)
    {
      error ("Could not lock spawner mutex");
      return;
    }

  if (!s_ctx->done)
    {
      s_ctx->done = true;
      if (pthread_cond_signal (&s_ctx->cond_sem) != 0)
        error ("Could not signal spawner conditional semaphore");

      if (queue_cancel (&s_ctx->rctx->jobs_queue) != QS_SUCCESS)
        error ("Could not cancel jobs queue");

      event_base_loopbreak (s_ctx->rctx->ev_base);
    }

  if (pthread_mutex_unlock (&s_ctx->mutex) != 0)
    error ("Could not unlock spawner mutex");
}

static void
schedule_timer_for_head (spawner_context_t *spawner_ctx)
{
  pending_task_t top;
  if (priority_queue_top (&spawner_ctx->pending_tasks, &top) != S_SUCCESS)
    return;

  struct timeval now;
  evutil_gettimeofday (&now, NULL);
  struct timeval delay;
  evutil_timersub (&top.deadline, &now, &delay);
  if (delay.tv_sec < 0
      || (delay.tv_sec == 0 && delay.tv_usec < MIN_DELAY_USEC))
    delay.tv_sec = 0, delay.tv_usec = MIN_DELAY_USEC;

  event_del (spawner_ctx->timer_event);
  if (event_add (spawner_ctx->timer_event, &delay) != 0)
    error ("Could not add timer");

  trace ("Scheduled timer for priority queue head (%ld.%06ld s)",
         (long)delay.tv_sec, (long)delay.tv_usec);
}

static void
timer_callback (evutil_socket_t fd, short what, void *arg)
{
  (void)fd;
  (void)what;
  spawner_context_t *spawner_ctx = arg;

  event_del (spawner_ctx->timer_event);

  pending_task_t due;
  if (priority_queue_top (&spawner_ctx->pending_tasks, &due) != S_SUCCESS)
    return;
  if (priority_queue_pop (&spawner_ctx->pending_tasks) != S_SUCCESS)
    return;

  result_t *result = &due.result.result;
  result->is_correct = false;

  pthread_mutex_lock (&due.ctx->rctr_conn.is_writing_mutex);
  bool is_writing = due.ctx->rctr_conn.is_writing;
  due.ctx->rctr_conn.is_writing = true;
  pthread_mutex_unlock (&due.ctx->rctr_conn.is_writing_mutex);

  if (is_writing)
    {
      queue_push_back (&due.ctx->result_queue, result);
      return;
    }

  memcpy (&due.ctx->result_buffer, result, sizeof (*result));
  result_write_state_setup (due.ctx);

  if (push_job (due.ctx->rctr_conn.rctr_ctx, due.ctx, send_result_job)
      != S_SUCCESS)
    {
      error ("Could not push send result job");
      return;
    }

  trace ("Timer fired: popped pending task, pushed send job");

  schedule_timer_for_head (spawner_ctx);
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
  ctx->read_state.is_partial = (ctx->read_state.vec[0].iov_len != 0);
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
          pending_task_t pending = {
            .ctx = ctx,
            .result = ctx->read_buffer,
          };
          result_t *result = &pending.result.result;
          result->is_correct = false;
          memset (result->password, 0, sizeof (result->password));

          evutil_gettimeofday (&pending.deadline, NULL);
          long timeout_ms
              = TASK_TIMEOUT_MS_MIN
                + (long)(rand_r (&ctx->rand_seed)
                         % (TASK_TIMEOUT_MS_MAX - TASK_TIMEOUT_MS_MIN + 1));
          pending.deadline.tv_sec += timeout_ms / MS_IN_SEC;
          pending.deadline.tv_usec
              += (timeout_ms % MS_IN_SEC) * USEC_IN_MSEC;
          if (pending.deadline.tv_usec >= USEC_IN_SEC)
            {
              pending.deadline.tv_sec++;
              pending.deadline.tv_usec -= USEC_IN_SEC;
            }

          if (priority_queue_push (&ctx->s_ctx->pending_tasks, &pending)
              != S_SUCCESS)
            {
              error ("Could not push pending task");
              /* Destroy */
              return;
            }
          schedule_timer_for_head (ctx->s_ctx);
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

void *
spawner_thread (void *arg)
{
  spawner_context_t *s_ctx = *(spawner_context_t **)arg;

  for (long i = 0; i < s_ctx->config->number_of_threads; ++i)
    {
      client_context_t *ctx = calloc (sizeof (client_context_t), 1);
      trace ("Creating load client %ld", i);
      if (client_context_init (ctx, s_ctx) == S_FAILURE)
        return (NULL);

      if (srv_connect (&ctx->client_base) == S_FAILURE)
        return (NULL);

      if (reactor_conn_init (&ctx->rctr_conn, s_ctx->rctx,
                             ctx->client_base.socket_fd, handle_read, ctx)
          == S_FAILURE)
        {
          error ("Could not initialize reactor connection");
          client_context_destroy (ctx);
          return (NULL);
        }
    }

  return (NULL);
}

status_t
spawn_load_clients (config_t *config)
{
  evthread_use_pthreads ();
  spawner_context_t ctx;
  reactor_context_t rctx;

  if (spawner_context_init (&ctx, &rctx, config) == S_FAILURE)
    return (S_FAILURE);

  spawner_context_t *spawner_ptr = &ctx;

  if (!thread_create (&ctx.thread_pool, spawner_thread, &spawner_ptr,
                      sizeof (spawner_ptr), "load client spawner"))
    {
      error ("Could not create spawner thread");
      goto cleanup;
    }

  if (create_reactor_threads (&ctx.thread_pool, config, ctx.rctx) == S_FAILURE)
    goto cleanup;

  if (pthread_mutex_lock (&ctx.mutex) != 0)
    {
      error ("Could not lock spawner mutex");
      goto cleanup;
    }

  while (!ctx.done)
    if (pthread_cond_wait (&ctx.cond_sem, &ctx.mutex) != 0)
      {
        error ("Could not wait on spawner condition");
        break;
      }

  if (pthread_mutex_unlock (&ctx.mutex) != 0)
    error ("Could not unlock spawner mutex");

cleanup:
  spawner_context_destroy (&ctx);
  return (S_FAILURE);
}
