#include "reactor_server.h"

#include "brute.h"
#include "brute_engine.h"
#include "common.h"
#include "log.h"
#include "queue.h"
#include "server_common.h"
#include "thread_pool.h"

#include <assert.h>
#include <errno.h>
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

typedef enum push_status
{
  PS_SUCCESS,
  PS_SKIPPED,
  PS_FAILURE,
  PS_DESTROYED,
} push_status_t;

typedef enum
{
  CS_ACTIVE,
  CS_WRITING,
  CS_STARVING,
  CS_CLOSING,
} client_state_t;

typedef struct rsrv_context_t
{
  srv_listener_t listener;
  brute_engine_t engine;
  thread_pool_t thread_pool;
  config_t *config;

  queue_t jobs_queue;
  queue_t starving_clients;
  struct event_base *ev_base;
  bool shutting_down;
} rsrv_context_t;

typedef struct io_state_t
{
  struct iovec vec[2];
  int32_t vec_sz;
  command_t cmd;
} io_state_t;

typedef struct write_state_t
{
  io_state_t base_state;
  struct iovec vec_extra[3];
  command_t cmd_extra;
  int32_t length;
  int32_t vec_extra_sz;
} write_state_t;

typedef struct task_slot_t
{
  bool in_use;
  task_t task;
} task_slot_t;

typedef struct client_context_t
{
  struct event *read_event;
  rsrv_context_t *rsrv_ctx;
  evutil_socket_t socket_fd;
  client_state_t state;
  task_slot_t task_slots[QUEUE_SIZE];
  queue_t free_slot_idx;
  write_state_t write_state;
  io_state_t read_state;
  result_t read_buffer;
  pthread_mutex_t mutex;
  _Atomic int ref_count;
} client_context_t;

typedef struct event_node_t
{
  const struct event *ev;
  struct event_node_t *next;
  struct event_node_t *prev;
} event_node_t;

typedef struct event_list_t
{
  event_node_t head;
} event_list_t;

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

static void client_context_destroy (client_context_t *ctx);

static bool
client_unref (client_context_t *ctx)
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

static void
client_release_event_ref (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  struct event *ev = ctx->read_event;
  ctx->read_event = NULL;
  pthread_mutex_unlock (&ctx->mutex);

  if (!ev)
    return;

  if (event_del (ev) == -1)
    error ("Could not delete read event");
  event_free (ev);

  client_unref (ctx);
}

static bool
client_is_closing (client_context_t *ctx)
{
  pthread_mutex_lock (&ctx->mutex);
  bool is_closing = ctx->state == CS_CLOSING;
  pthread_mutex_unlock (&ctx->mutex);

  return (is_closing);
}

static void
jobs_queue_unref_cb (void *payload, void *arg)
{
  (void)arg;
  job_t *job = payload;
  client_unref (job->arg);
}

static void
starving_clients_unref_cb (void *payload, void *arg)
{
  (void)arg;
  client_context_t *client = *(client_context_t **)payload;
  client_unref (client);
}

static int
collect_events_cb (const struct event_base *ev_base, const struct event *ev,
                   void *arg)
{
  (void)ev_base; /* to suppress the "unused parameter" warning */

  event_list_t *list = arg;
  event_node_t *node = calloc (1, sizeof (*node));
  if (!node)
    return -1;
  node->next = &list->head;
  node->prev = list->head.prev;
  node->ev = ev;

  list->head.prev->next = node;
  list->head.prev = node;

  return 0;
}

static void handle_read (evutil_socket_t socket_fd, short what, void *arg);

static void
clients_cleanup (event_list_t *list)
{
  event_node_t *curr = list->head.next;
  while (curr->ev)
    {
      event_node_t *dummy = curr;
      if (event_get_callback (curr->ev) == handle_read)
        {
          client_context_t *ctx = event_get_callback_arg (curr->ev);
          if (ctx)
            {
              trace ("Releasing client event reference");
              client_release_event_ref (ctx);
            }
        }

      curr->prev->next = curr->next;
      curr->next->prev = curr->prev;
      curr = curr->next;
      free (dummy);
    }
}

static client_context_t *
client_context_init (rsrv_context_t *rsrv_ctx, evutil_socket_t fd)
{
  client_context_t *client_ctx = calloc (1, sizeof (*client_ctx));
  if (!client_ctx)
    {
      error ("Could not allocate client context");
      return (NULL);
    }

  if (pthread_mutex_init (&client_ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize client mutex");
      free (client_ctx);
      return (NULL);
    }
  client_ctx->state = CS_ACTIVE;

  client_ctx->socket_fd = fd;
  client_ctx->rsrv_ctx = rsrv_ctx;
  client_ctx->read_state.vec[0].iov_base = &client_ctx->read_buffer;
  client_ctx->read_state.vec[0].iov_len = sizeof (client_ctx->read_buffer);
  client_ctx->read_state.vec_sz = 1;

  config_t *config = client_ctx->rsrv_ctx->config;

  write_state_t *write_state = &client_ctx->write_state;
  io_state_t *write_state_base = &write_state->base_state;

  /* Set up vector for hash sending */
  write_state_base->cmd = CMD_HASH;
  write_state_base->vec[0].iov_base = &write_state_base->cmd;
  write_state_base->vec[0].iov_len = sizeof (write_state_base->cmd);
  write_state_base->vec[1].iov_base = config->hash;
  write_state_base->vec[1].iov_len = HASH_LENGTH;
  write_state_base->vec_sz = 2;

  /* Set up vector for alphabet sending */
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

  if (evutil_make_socket_nonblocking (client_ctx->socket_fd) != 0)
    {
      error ("Could not change socket to be nonblocking");
      goto fail;
    }

  client_ctx->ref_count = 1;
  client_ctx->read_event
      = event_new (rsrv_ctx->ev_base, client_ctx->socket_fd,
                   EV_READ | EV_PERSIST, handle_read, client_ctx);
  if (!client_ctx->read_event)
    {
      error ("Could not create read event");
      goto fail;
    }

  client_try_ref (client_ctx);

  return (client_ctx);

fail:
  if (client_ctx->read_event)
    event_free (client_ctx->read_event);

  if (free_slot_idx_initialized
      && queue_destroy (&client_ctx->free_slot_idx) != QS_SUCCESS)
    error ("Could not destroy free slot queue");

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
  bool starving_clients_initialized = false;
  bool jobs_queue_initialized = false;

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

  if (queue_init (&ctx->jobs_queue, sizeof (job_t)) == QS_FAILURE)
    {
      error ("Could not initialize jobs queue");
      goto fail;
    }
  jobs_queue_initialized = true;

  ctx->ev_base = event_base_new ();
  if (!ctx->ev_base)
    {
      error ("Could not initialize event base");
      goto fail;
    }

  if (evutil_make_socket_nonblocking (ctx->listener.listen_fd) < 0)
    {
      error ("Could not change socket to be nonblocking");
      goto fail;
    }

  return S_SUCCESS;

fail:
  if (ctx->ev_base)
    {
      event_base_free (ctx->ev_base);
      ctx->ev_base = NULL;
    }

  if (jobs_queue_initialized && queue_destroy (&ctx->jobs_queue) == QS_FAILURE)
    error ("Could not destroy jobs queue during init cleanup");

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

  event_base_loopbreak (ctx->ev_base);

  if (queue_cancel (&ctx->jobs_queue) == QS_FAILURE)
    error ("Could not cancel jobs queue");

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
  if (queue_drain (&ctx->jobs_queue, jobs_queue_unref_cb, NULL) != QS_SUCCESS)
    error ("Could not drain jobs queue");

  if (queue_drain (&ctx->starving_clients, starving_clients_unref_cb, NULL)
      != QS_SUCCESS)
    error ("Could not drain starving clients queue");

  if (queue_destroy (&ctx->jobs_queue) == QS_FAILURE)
    return S_FAILURE;

  if (queue_destroy (&ctx->starving_clients) == QS_FAILURE)
    return S_FAILURE;

  event_list_t ev_list;
  ev_list.head.prev = &ev_list.head;
  ev_list.head.next = &ev_list.head;
  ev_list.head.ev = NULL;

  event_base_foreach_event (ctx->ev_base, collect_events_cb, &ev_list);
  clients_cleanup (&ev_list);
  event_base_free (ctx->ev_base);

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

  return (status);
}

static void
client_context_destroy (client_context_t *ctx)
{
  /* Freed in client_release_event_ref. */
  assert (ctx->read_event == NULL);
  assert (atomic_load_explicit (&ctx->ref_count, memory_order_relaxed) == 0);

  if (!ctx->rsrv_ctx->shutting_down && return_tasks (ctx) == S_FAILURE)
    error ("Could not return tasks to global queue");

  pthread_mutex_destroy (&ctx->mutex);
  trace ("Destroyed client mutex");

  if (queue_destroy (&ctx->free_slot_idx) != QS_SUCCESS)
    error ("Could not destroy free slot queue");

  trace ("Destroyed free slot queue");

  shutdown (ctx->socket_fd, SHUT_RDWR);
  if (close (ctx->socket_fd) != 0)
    error ("Could not close client socket");
  else
    trace ("Closed client socket");

  free (ctx);
  trace ("Destroyed client context");
}

static push_status_t
push_job_checked (client_context_t *ctx, status_t (*job_func) (void *))
{
  if (!client_try_ref (ctx))
    return (PS_SKIPPED);

  job_t job = {
    .arg = ctx,
    .job_func = job_func,
  };
  if (queue_push_back (&ctx->rsrv_ctx->jobs_queue, &job) != QS_SUCCESS)
    {
      error ("Could not push job to a job queue");
      return (client_unref (ctx) ? PS_DESTROYED : PS_FAILURE);
    }

  return (PS_SUCCESS);
}

static status_t
schedule_job (client_context_t *ctx, status_t (*job_func) (void *))
{
  return (push_job_checked (ctx, job_func) == PS_FAILURE ? S_FAILURE
                                                         : S_SUCCESS);
}

static status_t
write_state_write_wrapper (int socket_fd, struct iovec *vec, int *vec_sz)
{
  ssize_t written = writev (socket_fd, vec, *vec_sz);
  if (written < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return (S_SUCCESS);

      error ("Could not send config data to client");
      return (S_FAILURE);
    }
  if (written == 0)
    {
      error ("Could not send config data to client");
      return (S_FAILURE);
    }

  size_t bytes_written = written;
  int i = 0;
  while (i < *vec_sz && bytes_written > 0 && vec[i].iov_len <= bytes_written)
    bytes_written -= vec[i++].iov_len;

  *vec_sz -= i;
  memmove (&vec[0], &vec[i], sizeof (struct iovec) * (size_t)*vec_sz);

  if (*vec_sz > 0 && bytes_written > 0)
    {
      vec[0].iov_base = (char *)vec[0].iov_base + bytes_written;
      vec[0].iov_len -= bytes_written;
    }

  return (S_SUCCESS);
}

static inline status_t
write_state_write (int socket_fd, write_state_t *write_state)
{
  return (write_state_write_wrapper (socket_fd, write_state->base_state.vec,
                                     &write_state->base_state.vec_sz));
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

static status_t create_task_job (void *arg);

static status_t
send_config_job (void *arg)
{
  client_context_t *ctx = arg;

  if (client_is_closing (ctx))
    return (S_SUCCESS);

  status_t status;
  if (ctx->write_state.base_state.vec_sz != 0)
    {
      status = write_state_write (ctx->socket_fd, &ctx->write_state);
      if (status != S_SUCCESS)
        {
          error ("Could not send hash to client");
          client_disconnect (ctx);
          return (S_SUCCESS);
        }

      if (ctx->write_state.base_state.vec_sz != 0)
        return (schedule_job (ctx, send_config_job));
    }

  status = write_state_write_extra (ctx->socket_fd, &ctx->write_state);
  if (status != S_SUCCESS)
    {
      error ("Could not send alphabet to client");
      client_disconnect (ctx);
      return (S_SUCCESS);
    }

  return (schedule_job (ctx, ctx->write_state.vec_extra_sz != 0
                                 ? send_config_job
                                 : create_task_job));
}

static status_t send_task_job (void *arg);

static status_t
create_task_job (void *arg)
{
  client_context_t *ctx = arg;

  pthread_mutex_lock (&ctx->mutex);
  if (ctx->state == CS_CLOSING || ctx->state == CS_WRITING
      || ctx->state == CS_STARVING)
    {
      pthread_mutex_unlock (&ctx->mutex);
      return (S_SUCCESS);
    }
  ctx->state = CS_WRITING;
  pthread_mutex_unlock (&ctx->mutex);

  int id;
  queue_status_t qs;

  qs = queue_trypop (&ctx->free_slot_idx, &id);
  if (qs != QS_SUCCESS)
    {
      if (qs != QS_EMPTY)
        client_disconnect (ctx);
      goto clear_writing_flag;
    }

  task_t *task = &ctx->task_slots[id].task;

  qs = brute_engine_try_take_task (&ctx->rsrv_ctx->engine, task);
  if (qs == QS_EMPTY)
    {
      trace ("No tasks in global queue, add to starving clients");
      pthread_mutex_lock (&ctx->mutex);
      ctx->state = CS_STARVING;
      pthread_mutex_unlock (&ctx->mutex);

      if (queue_push_back (&ctx->free_slot_idx, &id) == QS_FAILURE)
        {
          error ("Could not push index to free slot queue");
          client_disconnect (ctx);
          return (S_SUCCESS);
        }

      trace ("Returned slot index %d to free slot queue", id);

      if (!client_try_ref (ctx))
        return (S_SUCCESS);
      if (queue_push_back (&ctx->rsrv_ctx->starving_clients, &ctx)
          == QS_FAILURE)
        {
          error ("Could not push client to starving clients queue");
          if (client_unref (ctx))
            return (S_SUCCESS);
          client_disconnect (ctx);
        }

      trace ("Queued client in starving clients queue");
      return (S_SUCCESS);
    }

  if (qs == QS_FAILURE)
    {
      if (queue_push_back (&ctx->free_slot_idx, &id) != QS_SUCCESS)
        error ("Could not push back id to free slot queue");
      client_disconnect (ctx);
      goto clear_writing_flag;
    }

  pthread_mutex_lock (&ctx->mutex);
  ctx->task_slots[id].in_use = true;
  pthread_mutex_unlock (&ctx->mutex);

  task_write_state_setup (ctx, id);

  return (schedule_job (ctx, send_task_job));

clear_writing_flag:
  pthread_mutex_lock (&ctx->mutex);
  if (ctx->state == CS_WRITING)
    ctx->state = CS_ACTIVE;
  pthread_mutex_unlock (&ctx->mutex);
  return (S_SUCCESS);
}

static status_t
send_task_job (void *arg)
{
  client_context_t *ctx = arg;

  if (client_is_closing (ctx))
    return (S_SUCCESS);

  status_t status = write_state_write (ctx->socket_fd, &ctx->write_state);
  if (status != S_SUCCESS)
    {
      error ("Could not send task to client");
      client_disconnect (ctx);
      return (S_SUCCESS);
    }

  if (ctx->write_state.base_state.vec_sz != 0)
    return (schedule_job (ctx, send_task_job));

  trace ("Sent task to client");

  pthread_mutex_lock (&ctx->mutex);
  ctx->state = CS_ACTIVE;
  pthread_mutex_unlock (&ctx->mutex);

  return (schedule_job (ctx, create_task_job));
}

static void
handle_accept_error (struct evconnlistener *listener, void *arg)
{
  (void)listener;

  warn ("Got error on connection accept: %m");
  rsrv_context_t *ctx = arg;
  event_base_loopbreak (ctx->ev_base);
}

static void
handle_accept (struct evconnlistener *listener, evutil_socket_t fd,
               struct sockaddr *address, int socklen, void *ctx)
{
  /* address and socklen are useless for this function, listener is needed only
   * for evconnlistener_get_base () call, but we do not use it, so it is also
   * useless */
  (void)listener; /* to suppress "unused parameter" warning */
  (void)address;  /* to suppress "unused parameter" warning */
  (void)socklen;  /* to suppress "unused parameter" warning */

  rsrv_context_t *srv_ctx = ctx;

  client_context_t *client_ctx = client_context_init (srv_ctx, fd);
  if (!client_ctx)
    goto fail;

  trace ("Accepted client connection");

  if (event_add (client_ctx->read_event, NULL) != 0)
    {
      error ("Could not add event to event base");
      goto release_event_ref;
    }

  trace ("Registered client read event");

  if (schedule_job (client_ctx, send_config_job) == S_FAILURE)
    {
      error ("Could not add send_config job");
      goto release_event_ref;
    }

  client_unref (client_ctx);

  return;

release_event_ref:
  client_release_event_ref (client_ctx);
  client_unref (client_ctx);
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

  if (!client_try_ref (ctx))
    return;

  ssize_t nread
      = readv (ctx->socket_fd, ctx->read_state.vec, ctx->read_state.vec_sz);
  if (nread < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        goto out;

      error ("Could not read result from a client");
      client_disconnect (ctx);
      client_release_event_ref (ctx);
      goto out;
    }

  if (nread == 0)
    {
      error ("Client closed connection");
      client_disconnect (ctx);
      client_release_event_ref (ctx);
      goto out;
    }

  size_t bytes_read = (size_t)nread;

  ctx->read_state.vec[0].iov_len -= bytes_read;
  ctx->read_state.vec[0].iov_base
      = (char *)ctx->read_state.vec[0].iov_base + bytes_read;

  if (ctx->read_state.vec[0].iov_len > 0)
    goto out;

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

      event_base_loopbreak (ctx->rsrv_ctx->ev_base);
    }

  if (brute_engine_task_done (&ctx->rsrv_ctx->engine) == S_FAILURE)
    goto out;

  trace ("Received %s result %s with id %d from client",
         result->is_correct ? "correct" : "incorrect", result->password,
         result->id);

  pthread_mutex_lock (&ctx->mutex);
  bool is_writing = ctx->state == CS_WRITING;
  pthread_mutex_unlock (&ctx->mutex);

  if (!is_writing)
    {
      push_status_t ps = push_job_checked (ctx, create_task_job);
      if (ps == PS_FAILURE)
        error ("Could not schedule create task job from read event");

      if (ps == PS_SUCCESS)
        trace ("Pushed create task job from read event");
    }

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
      if (client->state == CS_CLOSING || client->state == CS_WRITING)
        {
          pthread_mutex_unlock (&client->mutex);
          client_unref (client);
          continue;
        }
      client->state = CS_WRITING;
      pthread_mutex_unlock (&client->mutex);

      int id;
      qs = queue_pop (&client->free_slot_idx, &id);
      if (qs == QS_INACTIVE)
        goto fail_starving_take;
      if (qs != QS_SUCCESS)
        {
          error ("Could not pop index from free slot queue");
          goto fail_starving_take;
        }

      task_t *task = &client->task_slots[id].task;
      if (brute_engine_take_task (&ctx->engine, task) != QS_SUCCESS)
        {
          error ("Could not pop from the global queue");
          goto fail_starving_take;
        }
      trace ("Got task for a starving client");

      task_write_state_setup (client, id);

      pthread_mutex_lock (&client->mutex);
      client->task_slots[id].in_use = true;
      pthread_mutex_unlock (&client->mutex);

      push_status_t ps = push_job_checked (client, send_task_job);
      switch (ps)
        {
        case PS_FAILURE:
          goto fail_starving_take;
        case PS_DESTROYED:
          continue;
        case PS_SKIPPED:
          client_unref (client);
          continue;
        case PS_SUCCESS:
          break;
        }

      client_unref (client);
      continue;

    fail_starving_take:
      client_disconnect (client);
      client_unref (client);
      return (NULL);
    }
}

static void *
handle_io (void *arg)
{
  rsrv_context_t *ctx = *(rsrv_context_t **)arg;

  for (;;)
    {
      job_t job;
      queue_status_t qs = queue_pop (&ctx->jobs_queue, &job);
      if (qs == QS_INACTIVE)
        break;
      if (qs != QS_SUCCESS)
        {
          error ("Could not pop a job from a job queue");
          break;
        }

      status_t st = job.job_func (job.arg);
      client_unref (job.arg);
      if (st == S_FAILURE)
        {
          error ("Could not complete a job");
          break;
        }
    }

  event_base_loopbreak (ctx->ev_base);
  return (NULL);
}

static void *
dispatch_event_loop (void *arg)
{
  rsrv_context_t *ctx = *(rsrv_context_t **)arg;
  if (event_base_dispatch (ctx->ev_base) != 0)
    error ("Could not dispatch the event loop");

  return (NULL);
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
      rsrv_ctx.ev_base, handle_accept, &rsrv_ctx, LEV_OPT_REUSEABLE, -1,
      rsrv_ctx.listener.listen_fd);

  if (!listener)
    goto cleanup;

  evconnlistener_set_error_cb (listener, handle_accept_error);

  thread_pool_t *thread_pool = &rsrv_ctx.thread_pool;

  long number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;

  if (create_threads (thread_pool, number_of_threads, handle_io, &context_ptr,
                      sizeof (context_ptr), "i/o handler")
      == 0)
    goto cleanup_listener;

  if (!thread_create (thread_pool, handle_starving_clients, &context_ptr,
                      sizeof (context_ptr), "starving clients handler"))
    goto cleanup_listener;

  if (!thread_create (thread_pool, dispatch_event_loop, &context_ptr,
                      sizeof (context_ptr), "event loop dispatcher"))
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
