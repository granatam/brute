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

#define DEBUG_CLIENT_REFS 1

typedef enum push_status
{
  PS_SUCCESS,
  PS_SKIPPED,
  PS_FAILURE,
} push_status_t;

typedef enum
{
  CS_ACTIVE,   /* idle: external scheduling allowed */
  CS_WRITING,  /* busy: one client job chain owns execution */
  CS_STARVING, /* queued in starving_clients */
  CS_CLOSING,
} client_state_t;

typedef struct rsrv_context_t
{
  srv_listener_t listener;
  brute_engine_t engine;
  thread_pool_t thread_pool;
  config_t *config;

  reactor_context_t rctr_ctx;
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
  reactor_conn_t conn;
  rsrv_context_t *rsrv_ctx;
  client_state_t state;
  task_slot_t task_slots[QUEUE_SIZE];
  queue_t free_slot_idx;
  write_state_t write_state;
  io_state_t read_state;
  result_t read_buffer;
  pthread_mutex_t mutex;
  _Atomic int ref_count;
  bool close_scheduled;
  bool close_cb_ref;
} client_context_t;

static void handle_read (evutil_socket_t socket_fd, short what, void *arg);
static void release_event_cb (evutil_socket_t fd, short what, void *arg);
static void client_context_destroy (client_context_t *ctx);
static status_t send_config_job (void *arg);
static status_t create_task_job (void *arg);
static status_t send_task_job (void *arg);

static const char *
client_state_name (client_state_t state)
{
  switch (state)
    {
    case CS_ACTIVE:
      return "ACTIVE";
    case CS_WRITING:
      return "WRITING";
    case CS_STARVING:
      return "STARVING";
    case CS_CLOSING:
      return "CLOSING";
    default:
      return "UNKNOWN";
    }
}

#if DEBUG_CLIENT_REFS
#define CLIENT_REF(ctx) client_ref_dbg ((ctx), __func__, __LINE__)
#define CLIENT_UNREF(ctx) client_unref_dbg ((ctx), __func__, __LINE__)
#define CLIENT_TRY_REF(ctx) client_try_ref_dbg ((ctx), __func__, __LINE__)
#else
#define CLIENT_REF(ctx) client_ref ((ctx))
#define CLIENT_UNREF(ctx) client_unref ((ctx))
#define CLIENT_TRY_REF(ctx) client_try_ref ((ctx))
#endif

static void
client_ref_raw (client_context_t *ctx)
{
  atomic_fetch_add_explicit (&ctx->ref_count, 1, memory_order_relaxed);
}

static bool
client_unref_raw (client_context_t *ctx)
{
  int old
      = atomic_fetch_sub_explicit (&ctx->ref_count, 1, memory_order_acq_rel);
  assert (old > 0);

  if (old == 1)
    {
      client_context_destroy (ctx);
      return true;
    }

  return false;
}

#if DEBUG_CLIENT_REFS
static void
client_ref_dbg (client_context_t *ctx, const char *func, int line)
{
  int old
      = atomic_fetch_add_explicit (&ctx->ref_count, 1, memory_order_relaxed);

  pthread_mutex_lock (&ctx->mutex);
  client_state_t state = ctx->state;
  bool close_scheduled = ctx->close_scheduled;
  bool close_cb_ref = ctx->close_cb_ref;
  struct event *read_event = ctx->conn.read_event;
  pthread_mutex_unlock (&ctx->mutex);

  trace ("CLIENT_REF   ctx=%p %d->%d state=%s read_event=%p "
         "close_scheduled=%d close_cb_ref=%d at %s:%d",
         (void *)ctx, old, old + 1, client_state_name (state),
         (void *)read_event, close_scheduled, close_cb_ref, func, line);
}

static bool
client_unref_dbg (client_context_t *ctx, const char *func, int line)
{
  int old = atomic_load_explicit (&ctx->ref_count, memory_order_relaxed);

  pthread_mutex_lock (&ctx->mutex);
  client_state_t state = ctx->state;
  bool close_scheduled = ctx->close_scheduled;
  bool close_cb_ref = ctx->close_cb_ref;
  struct event *read_event = ctx->conn.read_event;
  pthread_mutex_unlock (&ctx->mutex);

  trace ("CLIENT_UNREF ctx=%p %d->%d state=%s read_event=%p "
         "close_scheduled=%d close_cb_ref=%d at %s:%d",
         (void *)ctx, old, old - 1, client_state_name (state),
         (void *)read_event, close_scheduled, close_cb_ref, func, line);

  if (old <= 0)
    {
      error ("CLIENT_UNREF UNDERFLOW ctx=%p old=%d at %s:%d", (void *)ctx, old,
             func, line);
      assert (old > 0);
    }

  old = atomic_fetch_sub_explicit (&ctx->ref_count, 1, memory_order_acq_rel);

  if (old != 1)
    return false;

  trace ("CLIENT_DESTROY_TRIGGER ctx=%p at %s:%d", (void *)ctx, func, line);
  client_context_destroy (ctx);
  return true;
}

static bool
client_try_ref_dbg (client_context_t *ctx, const char *func, int line)
{
  pthread_mutex_lock (&ctx->mutex);

  if (ctx->state == CS_CLOSING)
    {
      int refs = atomic_load_explicit (&ctx->ref_count, memory_order_relaxed);
      trace ("CLIENT_TRY_REF_SKIP ctx=%p refs=%d state=%s read_event=%p "
             "close_scheduled=%d close_cb_ref=%d at %s:%d",
             (void *)ctx, refs, client_state_name (ctx->state),
             (void *)ctx->conn.read_event, ctx->close_scheduled,
             ctx->close_cb_ref, func, line);

      pthread_mutex_unlock (&ctx->mutex);
      return false;
    }

  int old
      = atomic_fetch_add_explicit (&ctx->ref_count, 1, memory_order_relaxed);

  trace ("CLIENT_TRY_REF_OK ctx=%p %d->%d state=%s read_event=%p "
         "close_scheduled=%d close_cb_ref=%d at %s:%d",
         (void *)ctx, old, old + 1, client_state_name (ctx->state),
         (void *)ctx->conn.read_event, ctx->close_scheduled, ctx->close_cb_ref,
         func, line);

  pthread_mutex_unlock (&ctx->mutex);
  return true;
}
#else
static void
client_ref (client_context_t *ctx)
{
  client_ref_raw (ctx);
}

static bool
client_unref (client_context_t *ctx)
{
  return client_unref_raw (ctx);
}

static bool
client_try_ref (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  if (ctx->state == CS_CLOSING)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return false;
    }

  client_ref_raw (ctx);
  pthread_mutex_unlock (&ctx->mutex);
  return true;
}
#endif

static void
client_job_release (void *arg)
{
  client_context_t *ctx = arg;
  trace ("CLIENT_JOB_RELEASE ctx=%p", (void *)ctx);
  CLIENT_UNREF (ctx);
}

static void
client_mark_closing (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  ctx->state = CS_CLOSING;
  pthread_mutex_unlock (&ctx->mutex);
}

static bool
client_is_closing (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  bool is_closing = ctx->state == CS_CLOSING;
  pthread_mutex_unlock (&ctx->mutex);

  return is_closing;
}

static void
client_busy_to_active (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  if (ctx->state == CS_WRITING)
    ctx->state = CS_ACTIVE;
  pthread_mutex_unlock (&ctx->mutex);
}

static void
client_busy_to_starving (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  if (ctx->state == CS_WRITING)
    ctx->state = CS_STARVING;
  pthread_mutex_unlock (&ctx->mutex);
}

static push_status_t
continue_client_job (client_context_t *ctx, status_t (*job_func) (void *))
{
  if (!CLIENT_TRY_REF (ctx))
    return PS_SKIPPED;

  if (push_job (&ctx->rsrv_ctx->rctr_ctx, ctx, job_func, client_job_release)
      != S_SUCCESS)
    {
      error ("Could not push job to a job queue");
      CLIENT_UNREF (ctx);
      return PS_SKIPPED;
    }

  return PS_SUCCESS;
}

static push_status_t
schedule_client_job (client_context_t *ctx, status_t (*job_func) (void *))
{
  pthread_mutex_lock (&ctx->mutex);

  if (ctx->state != CS_ACTIVE)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return PS_SKIPPED;
    }

  ctx->state = CS_WRITING;
  pthread_mutex_unlock (&ctx->mutex);

  push_status_t ps = continue_client_job (ctx, job_func);
  if (ps != PS_SUCCESS)
    client_busy_to_active (ctx);

  return ps;
}

static void
client_release_event_ref (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  struct event *ev = ctx->conn.read_event;
  if (ev)
    ctx->conn.read_event = NULL;
  pthread_mutex_unlock (&ctx->mutex);

  trace ("CLIENT_RELEASE_EVENT_REF ctx=%p ev=%p", (void *)ctx, (void *)ev);

  if (!ev)
    return;

  if (reactor_event_del_free (ev) == S_FAILURE)
    error ("Could not release client read event");

  CLIENT_UNREF (ctx); /* read_event owns this ref */
}

static void
release_event_cb (evutil_socket_t fd, short what, void *arg)
{
  (void)fd;
  (void)what;

  client_context_t *ctx = arg;

  pthread_mutex_lock (&ctx->mutex);
  bool had_cb_ref = ctx->close_cb_ref;
  ctx->close_cb_ref = false;
  pthread_mutex_unlock (&ctx->mutex);

  trace ("CLIENT_RELEASE_EVENT_CB ctx=%p had_cb_ref=%d", (void *)ctx,
         had_cb_ref);

  client_release_event_ref (ctx);

  if (had_cb_ref)
    CLIENT_UNREF (ctx);
}

static status_t return_tasks (client_context_t *ctx);

static void
client_return_tasks_on_close (client_context_t *ctx)
{
  if (ctx->rsrv_ctx->shutting_down)
    return;

  if (return_tasks (ctx) == S_FAILURE)
    error ("Could not return tasks from closing client");
}

static void
client_close_async (client_context_t *ctx)
{
  bool should_return_tasks = false;

  pthread_mutex_lock (&ctx->mutex);

  trace ("CLIENT_CLOSE_ASYNC ctx=%p state=%s close_scheduled=%d "
         "close_cb_ref=%d refs=%d",
         (void *)ctx, client_state_name (ctx->state), ctx->close_scheduled,
         ctx->close_cb_ref,
         atomic_load_explicit (&ctx->ref_count, memory_order_relaxed));

  if (ctx->state != CS_CLOSING)
    {
      ctx->state = CS_CLOSING;
      should_return_tasks = true;
    }

  if (ctx->close_scheduled)
    {
      pthread_mutex_unlock (&ctx->mutex);

      if (should_return_tasks)
        client_return_tasks_on_close (ctx);

      return;
    }

  ctx->close_scheduled = true;
  ctx->close_cb_ref = true;
  CLIENT_REF (ctx); /* release_event_cb owns this ref */

  pthread_mutex_unlock (&ctx->mutex);

  if (should_return_tasks)
    client_return_tasks_on_close (ctx);

  if (event_base_once (ctx->rsrv_ctx->rctr_ctx.ev_base, -1, EV_TIMEOUT,
                       release_event_cb, ctx, NULL)
      != 0)
    {
      error ("Could not schedule client event cleanup");

      pthread_mutex_lock (&ctx->mutex);
      bool had_cb_ref = ctx->close_cb_ref;
      ctx->close_scheduled = false;
      ctx->close_cb_ref = false;
      pthread_mutex_unlock (&ctx->mutex);

      client_release_event_ref (ctx);

      if (had_cb_ref)
        CLIENT_UNREF (ctx);
    }
}

static void
starving_clients_unref_cb (void *payload, void *arg)
{
  (void)arg;

  client_context_t *client = *(client_context_t **)payload;

  client_mark_closing (client);
  client_release_event_ref (client);

  CLIENT_UNREF (client); /* starving queue ref */
}

static void
release_client_event_if_read_cb (const struct event *ev, void *arg)
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
      return NULL;
    }

  if (pthread_mutex_init (&client_ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize client mutex");
      free (client_ctx);
      return NULL;
    }

  client_ctx->state = CS_ACTIVE;
  client_ctx->close_cb_ref = false;
  client_ctx->close_scheduled = false;

  client_ctx->conn.rctr_ctx = &rsrv_ctx->rctr_ctx;
  client_ctx->conn.fd = fd;
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

  bool free_slot_idx_initialized = false;
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

  if (evutil_make_socket_nonblocking (client_ctx->conn.fd) != 0)
    {
      error ("Could not change socket to be nonblocking");
      goto fail;
    }

  client_ctx->ref_count = 1;
  trace ("CLIENT_INIT ctx=%p refs=1 fd=%d", (void *)client_ctx,
         client_ctx->conn.fd);

  client_ctx->conn.read_event
      = event_new (rsrv_ctx->rctr_ctx.ev_base, client_ctx->conn.fd,
                   EV_READ | EV_PERSIST, handle_read, client_ctx);
  if (!client_ctx->conn.read_event)
    {
      error ("Could not create read event");
      goto fail;
    }

  CLIENT_REF (client_ctx); /* read_event owns this ref */

  return client_ctx;

fail:
  if (client_ctx->conn.read_event)
    event_free (client_ctx->conn.read_event);

  if (free_slot_idx_initialized
      && queue_destroy (&client_ctx->free_slot_idx) != QS_SUCCESS)
    error ("Could not destroy free slot queue");

  pthread_mutex_destroy (&client_ctx->mutex);
  free (client_ctx);
  return NULL;
}

static status_t
rsrv_context_init (rsrv_context_t *ctx, config_t *config)
{
  memset (ctx, 0, sizeof (*ctx));

  ctx->config = config;
  ctx->shutting_down = false;
  ctx->listener.listen_fd = -1;

  bool reactor_initialized = false;
  bool listener_initialized = false;
  bool engine_initialized = false;
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

  if (queue_init (&ctx->starving_clients, sizeof (client_context_t *))
      == QS_FAILURE)
    {
      error ("Could not initialize starving clients queue");
      goto fail;
    }
  starving_clients_initialized = true;

  if (reactor_context_init (&ctx->rctr_ctx) != S_SUCCESS)
    {
      error ("Could not initialize reactor context");
      goto fail;
    }
  reactor_initialized = true;

  if (evutil_make_socket_nonblocking (ctx->listener.listen_fd) < 0)
    {
      error ("Could not change socket to be nonblocking");
      goto fail;
    }

  return S_SUCCESS;

fail:
  if (reactor_initialized
      && reactor_context_destroy (&ctx->rctr_ctx) != S_SUCCESS)
    error ("Could not destroy reactor context during init cleanup");

  if (starving_clients_initialized
      && queue_destroy (&ctx->starving_clients) == QS_FAILURE)
    error ("Could not destroy starving clients queue during init cleanup");

  if (engine_initialized && brute_engine_destroy (&ctx->engine) == S_FAILURE)
    error ("Could not destroy brute engine during init cleanup");

  if (listener_initialized
      && srv_listener_destroy (&ctx->listener) == S_FAILURE)
    error ("Could not destroy server listener during init cleanup");

  return S_FAILURE;
}

static void
reactor_server_context_stop (rsrv_context_t *ctx)
{
  ctx->shutting_down = true;

  reactor_context_stop (&ctx->rctr_ctx);

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
  if (reactor_context_drain_jobs (&ctx->rctr_ctx) == S_FAILURE)
    error ("Could not drain jobs queue");

  if (queue_drain (&ctx->starving_clients, starving_clients_unref_cb, NULL)
      != QS_SUCCESS)
    error ("Could not drain starving clients queue");

  if (reactor_for_each_event_snapshot (&ctx->rctr_ctx,
                                       release_client_event_if_read_cb, NULL)
      == S_FAILURE)
    error ("Could not release client events");

  if (queue_destroy (&ctx->starving_clients) == QS_FAILURE)
    return S_FAILURE;

  if (reactor_context_destroy (&ctx->rctr_ctx) == S_FAILURE)
    return S_FAILURE;

  if (brute_engine_destroy (&ctx->engine) == S_FAILURE)
    return S_FAILURE;

  return S_SUCCESS;
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
  pthread_mutex_unlock (&ctx->mutex);

  return status;
}

static void
client_context_destroy (client_context_t *ctx)
{
  trace ("CLIENT_DESTROY_BEGIN ctx=%p read_event=%p refs=%d state=%s "
         "close_scheduled=%d close_cb_ref=%d",
         (void *)ctx, (void *)ctx->conn.read_event,
         atomic_load_explicit (&ctx->ref_count, memory_order_relaxed),
         client_state_name (ctx->state), ctx->close_scheduled,
         ctx->close_cb_ref);

  assert (ctx->conn.read_event == NULL);
  assert (atomic_load_explicit (&ctx->ref_count, memory_order_relaxed) == 0);

  if (!ctx->rsrv_ctx->shutting_down && return_tasks (ctx) == S_FAILURE)
    error ("Could not return tasks to global queue");

  pthread_mutex_destroy (&ctx->mutex);
  trace ("Destroyed client mutex");

  if (queue_destroy (&ctx->free_slot_idx) != QS_SUCCESS)
    error ("Could not destroy free slot queue");

  trace ("Destroyed free slot queue");

  shutdown (ctx->conn.fd, SHUT_RDWR);
  if (close (ctx->conn.fd) != 0)
    error ("Could not close client socket");
  else
    trace ("Closed client socket");

  free (ctx);
  trace ("Destroyed client context");
}

static inline status_t
write_state_write_extra (int socket_fd, write_state_t *write_state)
{
  return write_state_write_wrapper (socket_fd, write_state->vec_extra,
                                    &write_state->vec_extra_sz);
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
send_config_job (void *arg)
{
  client_context_t *ctx = arg;

  if (client_is_closing (ctx))
    return S_SUCCESS;

  status_t status;
  if (ctx->write_state.base_state.vec_sz != 0)
    {
      status = write_state_write (ctx->conn.fd, &ctx->write_state);
      if (status != S_SUCCESS)
        {
          error ("Could not send hash to client");
          client_close_async (ctx);
          return S_SUCCESS;
        }

      if (ctx->write_state.base_state.vec_sz != 0)
        return continue_client_job (ctx, send_config_job) == PS_FAILURE
                   ? S_FAILURE
                   : S_SUCCESS;
    }

  status = write_state_write_extra (ctx->conn.fd, &ctx->write_state);
  if (status != S_SUCCESS)
    {
      error ("Could not send alphabet to client");
      client_close_async (ctx);
      return S_SUCCESS;
    }

  return continue_client_job (ctx, ctx->write_state.vec_extra_sz != 0
                                       ? send_config_job
                                       : create_task_job)
                 == PS_FAILURE
             ? S_FAILURE
             : S_SUCCESS;
}

static status_t
create_task_job (void *arg)
{
  client_context_t *ctx = arg;

  if (client_is_closing (ctx))
    return S_SUCCESS;

  int id;
  queue_status_t qs = queue_trypop (&ctx->free_slot_idx, &id);
  if (qs != QS_SUCCESS)
    {
      if (qs != QS_EMPTY)
        client_mark_closing (ctx);

      client_busy_to_active (ctx);
      return S_SUCCESS;
    }

  task_t *task = &ctx->task_slots[id].task;

  qs = brute_engine_try_take_task (&ctx->rsrv_ctx->engine, task);
  if (qs == QS_EMPTY)
    {
      trace ("No tasks in global queue, add to starving clients");

      if (queue_push_back (&ctx->free_slot_idx, &id) == QS_FAILURE)
        {
          error ("Could not push index to free slot queue");
          client_mark_closing (ctx);
          return S_SUCCESS;
        }

      trace ("Returned slot index %d to free slot queue", id);

      client_busy_to_starving (ctx);

      if (!CLIENT_TRY_REF (ctx))
        return S_SUCCESS;

      if (queue_push_back (&ctx->rsrv_ctx->starving_clients, &ctx)
          == QS_FAILURE)
        {
          error ("Could not push client to starving clients queue");
          CLIENT_UNREF (ctx);
          client_mark_closing (ctx);
          return S_SUCCESS;
        }

      trace ("Queued client in starving clients queue");
      return S_SUCCESS;
    }

  if (qs == QS_FAILURE)
    {
      if (queue_push_back (&ctx->free_slot_idx, &id) != QS_SUCCESS)
        error ("Could not push back id to free slot queue");

      client_mark_closing (ctx);
      client_busy_to_active (ctx);
      return S_SUCCESS;
    }

  pthread_mutex_lock (&ctx->mutex);
  ctx->task_slots[id].in_use = true;
  pthread_mutex_unlock (&ctx->mutex);

  task_write_state_setup (ctx, id);

  return continue_client_job (ctx, send_task_job) == PS_FAILURE ? S_FAILURE
                                                                : S_SUCCESS;
}

static status_t
send_task_job (void *arg)
{
  client_context_t *ctx = arg;

  if (client_is_closing (ctx))
    return S_SUCCESS;

  status_t status = write_state_write (ctx->conn.fd, &ctx->write_state);
  if (status != S_SUCCESS)
    {
      error ("Could not send task to client");
      client_close_async (ctx);
      return S_SUCCESS;
    }

  if (ctx->write_state.base_state.vec_sz != 0)
    return continue_client_job (ctx, send_task_job) == PS_FAILURE ? S_FAILURE
                                                                  : S_SUCCESS;

  trace ("Sent task to client");

  client_busy_to_active (ctx);
  return S_SUCCESS;
}

static void
handle_accept_error (struct evconnlistener *listener, void *arg)
{
  (void)listener;

  warn ("Got error on connection accept: %m");
  rsrv_context_t *ctx = arg;
  event_base_loopbreak (ctx->rctr_ctx.ev_base);
}

static void
handle_accept (struct evconnlistener *listener, evutil_socket_t fd,
               struct sockaddr *address, int socklen, void *ctx)
{
  (void)listener;
  (void)address;
  (void)socklen;

  rsrv_context_t *srv_ctx = ctx;

  client_context_t *client_ctx = client_context_init (srv_ctx, fd);
  if (!client_ctx)
    goto fail;

  trace ("Accepted client connection");

  if (event_add (client_ctx->conn.read_event, NULL) != 0)
    {
      error ("Could not add event to event base");
      goto release_event_ref;
    }

  trace ("Registered client read event");

  if (schedule_client_job (client_ctx, send_config_job) == PS_FAILURE)
    {
      error ("Could not add send_config job");
      goto release_event_ref;
    }

  CLIENT_UNREF (client_ctx);
  return;

release_event_ref:
  client_release_event_ref (client_ctx);
  CLIENT_UNREF (client_ctx);
  return;

fail:
  shutdown (fd, SHUT_RDWR);
  if (close (fd) != 0)
    error ("Could not close client socket");
}

static void
handle_read (evutil_socket_t socket_fd, short what, void *arg)
{
  assert (what == EV_READ);
  (void)socket_fd;

  client_context_t *ctx = arg;

  if (!CLIENT_TRY_REF (ctx))
    return;

  reactor_io_status_t io_status = reactor_readv_advance (
      ctx->conn.fd, ctx->read_state.vec, &ctx->read_state.vec_sz);

  if (io_status == RIO_PENDING)
    goto out;

  if (io_status == RIO_ERROR)
    {
      error ("Could not read result from a client");
      client_close_async (ctx);
      goto out;
    }

  if (io_status == RIO_CLOSED)
    {
      error ("Client closed connection");
      client_close_async (ctx);
      goto out;
    }

  ctx->read_state.vec[0].iov_len = sizeof (ctx->read_buffer);
  ctx->read_state.vec[0].iov_base = &ctx->read_buffer;
  ctx->read_state.vec_sz = 1;

  result_t *result = &ctx->read_buffer;
  if (result->id < 0 || result->id >= QUEUE_SIZE)
    {
      warn ("Unexpected result id: %d", result->id);
      client_close_async (ctx);
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
      client_close_async (ctx);
      goto out;
    }

  trace ("Returned slot index %d to free slot queue", result->id);

  if (result->is_correct)
    {
      if (brute_engine_report_result (&ctx->rsrv_ctx->engine, result->password)
          == S_FAILURE)
        goto out;

      event_base_loopbreak (ctx->rsrv_ctx->rctr_ctx.ev_base);
    }

  trace ("Calling brute_engine_task_done");
  if (brute_engine_task_done (&ctx->rsrv_ctx->engine) == S_FAILURE)
    goto out;
  trace ("brute_engine_task_done completed");

  trace ("Received %s result %s with id %d from client",
         result->is_correct ? "correct" : "incorrect", result->password,
         result->id);

  push_status_t ps = schedule_client_job (ctx, create_task_job);
  if (ps == PS_FAILURE)
    error ("Could not schedule create task job from read event");

  if (ps == PS_SUCCESS)
    trace ("Pushed create task job from read event");

out:
  CLIENT_UNREF (ctx);
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
        return NULL;
      if (qs != QS_SUCCESS)
        {
          error ("Could not pop from a starving clients queue");
          return NULL;
        }

      pthread_mutex_lock (&client->mutex);
      if (client->state != CS_STARVING)
        {
          pthread_mutex_unlock (&client->mutex);
          CLIENT_UNREF (client);
          continue;
        }

      client->state = CS_WRITING;
      pthread_mutex_unlock (&client->mutex);

      int id;
      qs = queue_pop (&client->free_slot_idx, &id);
      if (qs == QS_INACTIVE)
        {
          client_busy_to_starving (client);
          CLIENT_UNREF (client);
          continue;
        }
      if (qs != QS_SUCCESS)
        {
          error ("Could not pop index from free slot queue");
          goto fail_starving_take;
        }

      task_t *task = &client->task_slots[id].task;
      if (brute_engine_take_task (&ctx->engine, task) != QS_SUCCESS)
        {
          if (queue_push_back (&client->free_slot_idx, &id) != QS_SUCCESS)
            error ("Could not return slot index to free slot queue");

          client_busy_to_starving (client);
          CLIENT_UNREF (client);
          continue;
        }

      trace ("Got task for a starving client");

      task_write_state_setup (client, id);

      pthread_mutex_lock (&client->mutex);
      client->task_slots[id].in_use = true;
      pthread_mutex_unlock (&client->mutex);

      push_status_t ps = continue_client_job (client, send_task_job);
      if (ps != PS_SUCCESS)
        {
          client_mark_closing (client);
          CLIENT_UNREF (client);
          return NULL;
        }

      CLIENT_UNREF (client);
      continue;

    fail_starving_take:
      client_mark_closing (client);
      CLIENT_UNREF (client);
      return NULL;
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
    return false;

  struct evconnlistener *listener = evconnlistener_new (
      rsrv_ctx.rctr_ctx.ev_base, handle_accept, &rsrv_ctx, LEV_OPT_REUSEABLE,
      -1, rsrv_ctx.listener.listen_fd);

  if (!listener)
    goto cleanup;

  evconnlistener_set_error_cb (listener, handle_accept_error);

  thread_pool_t *thread_pool = &rsrv_ctx.thread_pool;
  reactor_context_t *rctr_ptr = &rsrv_ctx.rctr_ctx;

  if (create_reactor_threads (thread_pool, config, rctr_ptr) == S_FAILURE)
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

  return found;
}
