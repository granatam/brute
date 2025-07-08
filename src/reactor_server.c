#include "reactor_server.h"

#include "brute_engine.h"
#include "common.h"
#include "log.h"
#include "queue.h"
#include "reactor_common.h"
#include "server_common.h"
#include "thread_pool.h"

#include <assert.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/thread.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

typedef enum
{
  CS_ACTIVE,
  CS_WRITING,
  CS_STARVING,
  CS_CLOSING,
} client_state_t;

typedef enum client_advance_state_t
{
  CAS_IDLE,
  CAS_QUEUED,
  CAS_REQUEUED,
} client_advance_state_t;

typedef struct rsrv_context_t
{
  srv_listener_t listener;
  brute_engine_t engine;
  thread_pool_t thread_pool;
  config_t *config;

  reactor_context_t reactor;
  queue_t starving_clients;
  bool shutting_down;
} rsrv_context_t;

typedef struct task_slot_t
{
  bool in_use;
  task_t task;
} task_slot_t;

typedef struct client_context_t
{
  rsrv_context_t *rsrv_ctx;
  reactor_conn_t conn;
  client_state_t state;
  task_slot_t task_slots[QUEUE_SIZE];
  queue_t free_slot_idx;
  write_state_t write_state;
  io_state_t read_state;
  result_t read_buffer;
  pthread_mutex_t mutex;
  _Atomic int ref_count;
  client_advance_state_t advance_state;
  bool starving_queued;
  bool has_reserved_task;
  task_t reserved_task;
} client_context_t;

static void handle_read (evutil_socket_t socket_fd, short what, void *arg);
static void client_context_destroy (client_context_t *ctx);
static status_t client_advance_job (void *arg);

static void
client_disconnect (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  ctx->state = CS_CLOSING;
  pthread_mutex_unlock (&ctx->mutex);
}

static bool
client_try_ref (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  if (ctx->state == CS_CLOSING)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return (false);
    }

  atomic_fetch_add_explicit (&ctx->ref_count, 1, memory_order_relaxed);
  pthread_mutex_unlock (&ctx->mutex);
  return (true);
}

static bool
client_unref (client_context_t *ctx)
{
  int old
      = atomic_fetch_sub_explicit (&ctx->ref_count, 1, memory_order_acq_rel);
  assert (old > 0);

  if (old == 1)
    {
      client_context_destroy (ctx);
      return (true);
    }

  return (false);
}

static void
client_unref_release (void *arg)
{
  client_unref (arg);
}

static void
client_release_event_ref (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  struct event *ev = ctx->conn.read_event;
  ctx->conn.read_event = NULL;
  pthread_mutex_unlock (&ctx->mutex);

  if (!ev)
    return;

  if (reactor_event_del_free (ev) == S_FAILURE)
    error ("Could not delete/free client read event");

  client_unref (ctx);
}

static void
starving_clients_unref_cb (void *payload, void *arg)
{
  (void)arg;
  client_context_t *client = *(client_context_t **)payload;
  client_unref (client);
}

static void
release_client_event_visit (const struct event *ev, void *arg)
{
  (void)arg;

  if (event_get_callback (ev) != handle_read)
    return;

  client_context_t *ctx = event_get_callback_arg (ev);
  if (!ctx)
    return;

  trace ("Releasing client event reference");
  client_release_event_ref (ctx);
}

static client_context_t *
client_context_init (rsrv_context_t *rsrv_ctx, evutil_socket_t fd)
{
  client_context_t *client_ctx = calloc (1, sizeof (*client_ctx));
  if (!client_ctx)
    {
      error ("Could not allocate client context");
      shutdown (fd, SHUT_RDWR);
      if (close (fd) != 0)
        error ("Could not close client socket");
      return (NULL);
    }

  /* Make sure that we won't accidentally close the stdin on `goto fail`. */
  client_ctx->conn.fd = -1;

  bool mutex_initialized = false;
  bool free_slot_idx_initialized = false;

  if (pthread_mutex_init (&client_ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize client mutex");
      goto fail;
    }
  mutex_initialized = true;

  client_ctx->state = CS_ACTIVE;
  client_ctx->rsrv_ctx = rsrv_ctx;
  client_ctx->read_state.vec[0].iov_base = &client_ctx->read_buffer;
  client_ctx->read_state.vec[0].iov_len = sizeof (client_ctx->read_buffer);
  client_ctx->read_state.vec_sz = 1;

  config_t *config = client_ctx->rsrv_ctx->config;

  write_state_t *write_state = &client_ctx->write_state;
  io_state_t *write_state_base = &write_state->base_state;

  write_state_base->cmd = CMD_HASH;
  write_state_base->vec[0].iov_base = &write_state_base->cmd;
  write_state_base->vec[0].iov_len = sizeof (write_state_base->cmd);
  write_state_base->vec[1].iov_base = config->hash;
  write_state_base->vec[1].iov_len = HASH_LENGTH;
  write_state_base->vec_sz = 2;

  write_state->cmd_extra = CMD_ALPH;
  write_state->length = (int32_t)strlen (config->alph);
  write_state->vec_extra[0].iov_base = &write_state->cmd_extra;
  write_state->vec_extra[0].iov_len = sizeof (write_state->cmd_extra);
  write_state->vec_extra[1].iov_base = &write_state->length;
  write_state->vec_extra[1].iov_len = sizeof (write_state->length);
  write_state->vec_extra[2].iov_base = config->alph;
  write_state->vec_extra[2].iov_len = write_state->length;
  write_state->vec_extra_sz = 3;

  if (queue_init (&client_ctx->free_slot_idx, sizeof (int)) != QS_SUCCESS)
    {
      error ("Could not initialize free slot queue");
      goto fail;
    }

  free_slot_idx_initialized = true;
  for (int i = 0; i < QUEUE_SIZE; ++i)
    if (queue_push (&client_ctx->free_slot_idx, &i) != QS_SUCCESS)
      {
        error ("Could not push index to free slot queue");
        goto fail;
      }

  if (reactor_conn_init (&client_ctx->conn, &rsrv_ctx->reactor, fd,
                         handle_read, client_ctx)
      != S_SUCCESS)
    {
      error ("Could not initialize reactor connection");
      goto fail;
    }

  client_ctx->ref_count = 1;

  /* The read event owns one client reference until `client_release_event_ref`.
   */
  client_try_ref (client_ctx);

  return (client_ctx);

fail:
  reactor_conn_destroy (&client_ctx->conn);

  if (free_slot_idx_initialized
      && queue_destroy (&client_ctx->free_slot_idx) != QS_SUCCESS)
    error ("Could not destroy free slot queue");

  if (mutex_initialized)
    pthread_mutex_destroy (&client_ctx->mutex);

  free (client_ctx);
  return (NULL);
}

static status_t
rsrv_context_init (rsrv_context_t *ctx, config_t *config)
{
  memset (ctx, 0, sizeof (*ctx));

  ctx->config = config;
  ctx->shutting_down = false;
  ctx->listener.listen_fd = -1;

  bool listener_initialized = false;
  bool engine_initialized = false;
  bool reactor_initialized = false;
  bool starving_clients_initialized = false;

  if (srv_listener_init (&ctx->listener, config) == S_FAILURE)
    {
      error ("Could not initialize server listener");
      goto fail;
    }
  listener_initialized = true;

  if (brute_engine_init (&ctx->engine) == S_FAILURE)
    {
      error ("Could not initialize brute engine");
      goto fail;
    }
  engine_initialized = true;

  if (thread_pool_init (&ctx->thread_pool) == S_FAILURE)
    {
      error ("Could not initialize thread pool");
      goto fail;
    }

  if (reactor_context_init (&ctx->reactor) == S_FAILURE)
    {
      error ("Could not initialize reactor context");
      goto fail;
    }
  reactor_initialized = true;

  if (queue_init (&ctx->starving_clients, sizeof (client_context_t *))
      == QS_FAILURE)
    {
      error ("Could not initialize starving clients queue");
      goto fail;
    }
  starving_clients_initialized = true;

  if (evutil_make_socket_nonblocking (ctx->listener.listen_fd) < 0)
    {
      error ("Could not change socket to be nonblocking");
      goto fail;
    }

  return (S_SUCCESS);

fail:
  if (starving_clients_initialized
      && queue_destroy (&ctx->starving_clients) == QS_FAILURE)
    error ("Could not destroy starving clients queue during init cleanup");

  if (reactor_initialized
      && reactor_context_destroy (&ctx->reactor) == S_FAILURE)
    error ("Could not destroy reactor context during init cleanup");

  if (engine_initialized && brute_engine_destroy (&ctx->engine) == S_FAILURE)
    error ("Could not destroy brute engine during init cleanup");

  if (listener_initialized
      && srv_listener_destroy (&ctx->listener) == S_FAILURE)
    error ("Could not destroy server listener during init cleanup");

  return (S_FAILURE);
}

static void
reactor_server_context_stop (rsrv_context_t *ctx)
{
  ctx->shutting_down = true;

  reactor_context_stop (&ctx->reactor);

  if (queue_cancel (&ctx->starving_clients) == QS_FAILURE)
    error ("Could not cancel starving clients queue");

  if (brute_engine_cancel (&ctx->engine) == S_FAILURE)
    error ("Could not cancel brute engine");

  if (srv_listener_destroy (&ctx->listener) == S_FAILURE)
    error ("Could not destroy server listener");
}

static status_t
reactor_server_context_destroy (rsrv_context_t *ctx)
{
  if (reactor_context_drain_jobs (&ctx->reactor) != S_SUCCESS)
    error ("Could not drain reactor jobs queue");

  if (queue_drain (&ctx->starving_clients, starving_clients_unref_cb, NULL)
      != QS_SUCCESS)
    error ("Could not drain starving clients queue");

  if (queue_destroy (&ctx->starving_clients) == QS_FAILURE)
    return (S_FAILURE);

  if (reactor_for_each_event_snapshot (&ctx->reactor,
                                       release_client_event_visit, NULL)
      == S_FAILURE)
    error ("Could not snapshot reactor events");

  if (reactor_context_destroy (&ctx->reactor) == S_FAILURE)
    return (S_FAILURE);

  return (brute_engine_destroy (&ctx->engine));
}

static status_t
return_tasks (client_context_t *ctx)
{
  status_t status = S_SUCCESS;

  pthread_mutex_lock (&ctx->mutex);

  for (int i = 0; i < QUEUE_SIZE; ++i)
    {
      if (ctx->task_slots[i].in_use)
        {
          if (brute_engine_return_task (&ctx->rsrv_ctx->engine,
                                        &ctx->task_slots[i].task)
              != QS_SUCCESS)
            {
              status = S_FAILURE;
              break;
            }

          ctx->task_slots[i].in_use = false;
        }
    }

  if (status == S_SUCCESS && ctx->has_reserved_task)
    {
      if (brute_engine_return_task (&ctx->rsrv_ctx->engine,
                                    &ctx->reserved_task)
          != QS_SUCCESS)
        status = S_FAILURE;
      else
        ctx->has_reserved_task = false;
    }

  pthread_mutex_unlock (&ctx->mutex);

  return (status);
}

static void
client_context_destroy (client_context_t *ctx)
{
  assert (ctx->conn.read_event == NULL);
  assert (atomic_load_explicit (&ctx->ref_count, memory_order_relaxed) == 0);

  if (!ctx->rsrv_ctx->shutting_down && return_tasks (ctx) == S_FAILURE)
    error ("Could not return tasks to global queue");

  pthread_mutex_destroy (&ctx->mutex);
  trace ("Destroyed client mutex");

  if (queue_destroy (&ctx->free_slot_idx) != QS_SUCCESS)
    error ("Could not destroy free slot queue");

  trace ("Destroyed free slot queue");

  if (reactor_conn_destroy (&ctx->conn) != S_SUCCESS)
    error ("Could not destroy reactor connection");

  free (ctx);
  trace ("Destroyed client context");
}

static void
client_rollback_scheduled_ref (client_context_t *ctx)
{
  int old
      = atomic_fetch_sub_explicit (&ctx->ref_count, 1, memory_order_acq_rel);

  /*
   * Only used to roll back a speculative job reference created while the
   * caller/current job already owns another live reference.
   *
   * This rollback must not destroy the client.
   */
  assert (old > 1);
}

static reactor_push_status_t
schedule_client_advance (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);

  if (ctx->state == CS_CLOSING)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return (RPS_INACTIVE);
    }

  if (ctx->advance_state == CAS_QUEUED || ctx->advance_state == CAS_REQUEUED)
    {
      ctx->advance_state = CAS_REQUEUED;
      pthread_mutex_unlock (&ctx->mutex);
      return RPS_INACTIVE;
    }

  ctx->advance_state = CAS_QUEUED;
  atomic_fetch_add_explicit (&ctx->ref_count, 1, memory_order_relaxed);

  pthread_mutex_unlock (&ctx->mutex);

  reactor_push_status_t rps = reactor_push_job (
      &ctx->rsrv_ctx->reactor, ctx, client_advance_job, client_unref_release);
  if (rps != RPS_SUCCESS)
    {
      pthread_mutex_lock (&ctx->mutex);
      ctx->advance_state = CAS_IDLE;
      pthread_mutex_unlock (&ctx->mutex);

      client_rollback_scheduled_ref (ctx);
      return (rps);
    }

  return (RPS_SUCCESS);
}

static reactor_push_status_t
requeue_client_advance (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);

  if (ctx->state == CS_CLOSING)
    {
      ctx->advance_state = CAS_IDLE;
      pthread_mutex_unlock (&ctx->mutex);
      return (RPS_INACTIVE);
    }

  atomic_fetch_add_explicit (&ctx->ref_count, 1, memory_order_relaxed);

  pthread_mutex_unlock (&ctx->mutex);

  reactor_push_status_t rps = reactor_push_job (
      &ctx->rsrv_ctx->reactor, ctx, client_advance_job, client_unref_release);
  if (rps != RPS_SUCCESS)
    {
      pthread_mutex_lock (&ctx->mutex);
      ctx->advance_state = CAS_IDLE;
      pthread_mutex_unlock (&ctx->mutex);

      client_rollback_scheduled_ref (ctx);
      return (rps);
    }

  return (RPS_SUCCESS);
}

static status_t
finish_client_advance (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);

  if (ctx->state == CS_CLOSING || ctx->advance_state != CAS_REQUEUED)
    {
      ctx->advance_state = CAS_IDLE;
      pthread_mutex_unlock (&ctx->mutex);
      return (S_SUCCESS);
    }

  ctx->advance_state = CAS_QUEUED;
  atomic_fetch_add_explicit (&ctx->ref_count, 1, memory_order_relaxed);

  pthread_mutex_unlock (&ctx->mutex);

  reactor_push_status_t rps = reactor_push_job (
      &ctx->rsrv_ctx->reactor, ctx, client_advance_job, client_unref_release);
  if (rps != RPS_SUCCESS)
    {
      pthread_mutex_lock (&ctx->mutex);
      ctx->advance_state = CAS_IDLE;
      pthread_mutex_unlock (&ctx->mutex);

      client_rollback_scheduled_ref (ctx);
      return (rps == RPS_INACTIVE ? S_SUCCESS : S_FAILURE);
    }

  return (S_SUCCESS);
}

static inline status_t
write_state_write_extra (int socket_fd, write_state_t *write_state)
{
  return (write_state_write_wrapper (socket_fd, write_state->vec_extra,
                                     &write_state->vec_extra_sz));
}

static void
task_write_state_setup (client_context_t *ctx, int id)
{
  task_t *task = &ctx->task_slots[id].task;
  task->result.id = id;
  task->to = task->from;
  task->from = 0;
  task->result.is_correct = false;

  io_state_t *write_state_base = &ctx->write_state.base_state;
  write_state_base->cmd = CMD_TASK;
  write_state_base->vec[0].iov_base = &write_state_base->cmd;
  write_state_base->vec[0].iov_len = sizeof (write_state_base->cmd);
  write_state_base->vec[1].iov_base = task;
  write_state_base->vec[1].iov_len = sizeof (*task);
  write_state_base->vec_sz = 2;
}

static status_t
queue_starving_client (client_context_t *ctx, int slot_id)
{
  if (queue_push_back (&ctx->free_slot_idx, &slot_id) != QS_SUCCESS)
    {
      error ("Could not return slot index to free slot queue");
      client_disconnect (ctx);
      return (S_SUCCESS);
    }

  pthread_mutex_lock (&ctx->mutex);

  if (ctx->state == CS_CLOSING)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return (S_SUCCESS);
    }

  ctx->state = CS_STARVING;

  if (ctx->starving_queued)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return (S_SUCCESS);
    }

  ctx->starving_queued = true;
  atomic_fetch_add_explicit (&ctx->ref_count, 1, memory_order_relaxed);

  pthread_mutex_unlock (&ctx->mutex);

  if (queue_push_back (&ctx->rsrv_ctx->starving_clients, &ctx) != QS_SUCCESS)
    {
      error ("Could not push client to starving clients queue");

      pthread_mutex_lock (&ctx->mutex);
      ctx->starving_queued = false;
      pthread_mutex_unlock (&ctx->mutex);

      client_unref (ctx);
      client_disconnect (ctx);
      return (S_SUCCESS);
    }

  trace ("Queued client in starving clients queue");
  return (S_SUCCESS);
}

static bool
client_take_reserved_task (client_context_t *ctx, task_t *task)
{
  pthread_mutex_lock (&ctx->mutex);

  if (!ctx->has_reserved_task)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return (false);
    }

  *task = ctx->reserved_task;
  ctx->has_reserved_task = false;

  pthread_mutex_unlock (&ctx->mutex);
  return (true);
}

static status_t
client_prepare_next_task (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);

  if (ctx->state == CS_CLOSING || ctx->state == CS_STARVING)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return (S_SUCCESS);
    }

  ctx->state = CS_WRITING;
  pthread_mutex_unlock (&ctx->mutex);

  int id;
  queue_status_t qs = queue_trypop (&ctx->free_slot_idx, &id);
  if (qs != QS_SUCCESS)
    {
      if (qs != QS_EMPTY)
        {
          error ("Could not pop index from free slot queue");
          client_disconnect (ctx);
        }

      pthread_mutex_lock (&ctx->mutex);
      if (ctx->state == CS_WRITING)
        ctx->state = CS_ACTIVE;
      pthread_mutex_unlock (&ctx->mutex);

      return (S_SUCCESS);
    }

  task_t task;
  if (!client_take_reserved_task (ctx, &task))
    {
      qs = brute_engine_try_take_task (&ctx->rsrv_ctx->engine, &task);

      if (qs == QS_EMPTY)
        return (queue_starving_client (ctx, id));

      if (qs != QS_SUCCESS)
        {
          error ("Could not take task from global queue");

          if (queue_push_back (&ctx->free_slot_idx, &id) != QS_SUCCESS)
            error ("Could not push back id to free slot queue");

          client_disconnect (ctx);
          return (S_SUCCESS);
        }
    }

  pthread_mutex_lock (&ctx->mutex);

  if (ctx->state == CS_CLOSING)
    {
      pthread_mutex_unlock (&ctx->mutex);

      if (!ctx->rsrv_ctx->shutting_down
          && brute_engine_return_task (&ctx->rsrv_ctx->engine, &task)
                 != QS_SUCCESS)
        error ("Could not return task to global queue");

      if (queue_push_back (&ctx->free_slot_idx, &id) != QS_SUCCESS)
        error ("Could not push back id to free slot queue");

      return (S_SUCCESS);
    }

  ctx->task_slots[id].task = task;
  ctx->task_slots[id].in_use = true;

  pthread_mutex_unlock (&ctx->mutex);

  task_write_state_setup (ctx, id);

  return (S_SUCCESS);
}

static status_t
client_advance_job (void *arg)
{
  client_context_t *ctx = arg;

  for (;;)
    {
      pthread_mutex_lock (&ctx->mutex);
      bool is_closing = ctx->state == CS_CLOSING;
      pthread_mutex_unlock (&ctx->mutex);
      if (is_closing)
        return (finish_client_advance (ctx));

      if (ctx->write_state.base_state.vec_sz != 0)
        {
          command_t cmd = ctx->write_state.base_state.cmd;

          status_t status
              = write_state_write (ctx->conn.fd, &ctx->write_state);
          if (status != S_SUCCESS)
            {
              error ("Could not write base message to client");
              client_disconnect (ctx);
              return (finish_client_advance (ctx));
            }

          if (ctx->write_state.base_state.vec_sz != 0)
            {
              reactor_push_status_t rps = requeue_client_advance (ctx);
              return (rps == RPS_FAILURE ? S_FAILURE : S_SUCCESS);
            }

          if (cmd == CMD_TASK)
            {
              trace ("Sent task to client");

              pthread_mutex_lock (&ctx->mutex);
              if (ctx->state == CS_WRITING)
                ctx->state = CS_ACTIVE;
              pthread_mutex_unlock (&ctx->mutex);

              continue;
            }
        }

      if (ctx->write_state.vec_extra_sz != 0)
        {
          status_t status
              = write_state_write_extra (ctx->conn.fd, &ctx->write_state);
          if (status != S_SUCCESS)
            {
              error ("Could not send alphabet to client");
              client_disconnect (ctx);
              return (finish_client_advance (ctx));
            }

          if (ctx->write_state.vec_extra_sz != 0)
            {
              reactor_push_status_t rps = requeue_client_advance (ctx);
              return (rps == RPS_FAILURE ? S_FAILURE : S_SUCCESS);
            }

          continue;
        }

      status_t status = client_prepare_next_task (ctx);
      if (status != S_SUCCESS)
        {
          client_disconnect (ctx);
          return (finish_client_advance (ctx));
        }

      if (ctx->write_state.base_state.vec_sz != 0
          && ctx->write_state.base_state.cmd == CMD_TASK)
        continue;

      return (finish_client_advance (ctx));
    }
}

static void
handle_accept_error (struct evconnlistener *listener, void *arg)
{
  (void)listener;

  warn ("Got error on connection accept: %m");
  rsrv_context_t *ctx = arg;
  event_base_loopbreak (ctx->reactor.ev_base);
}

static void
handle_accept (struct evconnlistener *listener, evutil_socket_t fd,
               struct sockaddr *address, int socklen, void *ctx)
{
  (void)listener; /* to suppress "unused parameter" warning */
  (void)address;  /* to suppress "unused parameter" warning */
  (void)socklen;  /* to suppress "unused parameter" warning */

  rsrv_context_t *srv_ctx = ctx;

  client_context_t *client_ctx = client_context_init (srv_ctx, fd);
  if (!client_ctx)
    return;

  trace ("Accepted client connection");

  if (reactor_conn_enable_read (&client_ctx->conn) != S_SUCCESS)
    {
      error ("Could not enable client read event");
      goto release_event_ref;
    }

  trace ("Registered client read event");

  if (schedule_client_advance (client_ctx) == RPS_FAILURE)
    {
      error ("Could not schedule client advance job");
      goto release_event_ref;
    }

  client_unref (client_ctx);

  return;

release_event_ref:
  client_release_event_ref (client_ctx);
  client_unref (client_ctx);
}

static void
handle_read (evutil_socket_t socket_fd, short what, void *arg)
{
  assert (what == EV_READ);
  /* We already have socket_fd in client_context_t */
  (void)socket_fd; /* to suppress "unused parameter" warning */

  client_context_t *ctx = arg;

  if (!client_try_ref (ctx))
    return;

  reactor_io_status_t rio = reactor_conn_readv (
      &ctx->conn, ctx->read_state.vec, &ctx->read_state.vec_sz);

  if (rio == RIO_PENDING)
    goto out;

  if (rio == RIO_ERROR)
    {
      error ("Could not read result from a client");
      client_disconnect (ctx);
      client_release_event_ref (ctx);
      goto out;
    }

  if (rio == RIO_CLOSED)
    {
      error ("Client closed connection");
      client_disconnect (ctx);
      client_release_event_ref (ctx);
      goto out;
    }

  assert (rio == RIO_DONE);

  ctx->read_state.vec[0].iov_len = sizeof (ctx->read_buffer);
  ctx->read_state.vec[0].iov_base = &ctx->read_buffer;
  ctx->read_state.vec_sz = 1;

  result_t *result = &ctx->read_buffer;
  if (result->id < 0 || result->id >= QUEUE_SIZE)
    {
      warn ("Unexpected result id: %d", result->id);
      client_disconnect (ctx);
      goto out;
    }

  pthread_mutex_lock (&ctx->mutex);
  bool is_used = ctx->task_slots[result->id].in_use;
  ctx->task_slots[result->id].in_use = false;
  pthread_mutex_unlock (&ctx->mutex);

  if (!is_used)
    {
      warn ("Unexpected result id: %d", result->id);
      goto out;
    }

  if (queue_push_back (&ctx->free_slot_idx, &result->id) != QS_SUCCESS)
    {
      error ("Could not return id to a queue");
      client_disconnect (ctx);
      goto out;
    }

  trace ("Returned slot index %d to free slot queue", result->id);

  if (result->is_correct)
    {
      if (brute_engine_report_result (&ctx->rsrv_ctx->engine, result->password)
          == S_FAILURE)
        goto out;

      event_base_loopbreak (ctx->rsrv_ctx->reactor.ev_base);
    }

  if (brute_engine_task_done (&ctx->rsrv_ctx->engine) == S_FAILURE)
    goto out;

  trace ("Received %s result %s with id %d from client",
         result->is_correct ? "correct" : "incorrect", result->password,
         result->id);

  reactor_push_status_t rps = schedule_client_advance (ctx);
  if (rps == RPS_FAILURE)
    error ("Could not schedule client advance job from read event");

  if (rps == RPS_SUCCESS)
    trace ("Scheduled client advance job from read event");

out:
  client_unref (ctx);
}

static void *
handle_starving_clients (void *arg)
{
  rsrv_context_t *ctx = *(rsrv_context_t **)arg;

  for (;;)
    {
      client_context_t *client = NULL;
      queue_status_t qs = queue_pop (&ctx->starving_clients, &client);
      if (qs == QS_INACTIVE)
        return (NULL);
      if (qs != QS_SUCCESS)
        {
          error ("Could not pop from a starving clients queue");
          return (NULL);
        }

      pthread_mutex_lock (&client->mutex);
      bool closing = client->state == CS_CLOSING;
      pthread_mutex_unlock (&client->mutex);

      if (closing)
        {
          pthread_mutex_lock (&client->mutex);
          client->starving_queued = false;
          pthread_mutex_unlock (&client->mutex);

          client_unref (client);
          continue;
        }

      task_t task;
      queue_status_t task_status
          = brute_engine_take_task (&ctx->engine, &task);
      if (task_status != QS_SUCCESS)
        {
          pthread_mutex_lock (&client->mutex);
          client->starving_queued = false;
          pthread_mutex_unlock (&client->mutex);

          if (!ctx->shutting_down && task_status != QS_INACTIVE
              && task_status != QS_EMPTY)
            {
              error ("Could not pop from the global queue");
              client_disconnect (client);
            }

          client_unref (client);
          return (NULL);
        }

      pthread_mutex_lock (&client->mutex);

      client->starving_queued = false;

      if (client->state == CS_CLOSING)
        {
          pthread_mutex_unlock (&client->mutex);

          if (!ctx->shutting_down
              && brute_engine_return_task (&ctx->engine, &task) != QS_SUCCESS)
            error ("Could not return task to global queue");

          client_unref (client);
          continue;
        }

      client->reserved_task = task;
      client->has_reserved_task = true;

      if (client->state == CS_STARVING)
        client->state = CS_ACTIVE;

      pthread_mutex_unlock (&client->mutex);

      reactor_push_status_t rps = schedule_client_advance (client);
      if (rps == RPS_FAILURE)
        error ("Could not schedule client advance from starving handler");

      client_unref (client);
    }
}

bool
run_reactor_server (task_t *task, config_t *config)
{
  evthread_use_pthreads ();
  signal (SIGPIPE, SIG_IGN);

  rsrv_context_t rsrv_ctx;
  rsrv_context_t *context_ptr = &rsrv_ctx;
  bool found = false;

  if (rsrv_context_init (&rsrv_ctx, config) == S_FAILURE)
    return (false);

  struct evconnlistener *listener = evconnlistener_new (
      rsrv_ctx.reactor.ev_base, handle_accept, &rsrv_ctx, LEV_OPT_REUSEABLE,
      -1, rsrv_ctx.listener.listen_fd);

  if (!listener)
    goto cleanup;

  evconnlistener_set_error_cb (listener, handle_accept_error);

  thread_pool_t *thread_pool = &rsrv_ctx.thread_pool;

  if (reactor_create_threads (thread_pool, config, &rsrv_ctx.reactor)
      == S_FAILURE)
    goto cleanup_listener;

  if (!thread_create (thread_pool, handle_starving_clients, &context_ptr,
                      sizeof (context_ptr), "starving clients handler"))
    goto cleanup_listener;

  if (brute_engine_run (&rsrv_ctx.engine, task, config, &found) == S_FAILURE)
    goto cleanup;

cleanup_listener:
  if (listener)
    evconnlistener_free (listener);

cleanup:
  reactor_server_context_stop (&rsrv_ctx);

  if (thread_pool_join (&rsrv_ctx.thread_pool) == S_FAILURE)
    error ("Could not join thread pool");

  if (reactor_server_context_destroy (&rsrv_ctx) == S_FAILURE)
    error ("Could not destroy reactor server context");

  return (found);
}
