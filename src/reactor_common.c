#include "reactor_common.h"

#include "log.h"

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

static void
reactor_job_cleanup_cb (void *payload, void *arg)
{
  (void)arg;

  reactor_job_t *job = payload;
  if (job->cleanup)
    job->cleanup (job->arg);
}

static void *
reactor_handle_io (void *arg)
{
  reactor_context_t *ctx = *(reactor_context_t **)arg;

  for (;;)
    {
      reactor_job_t job;
      queue_status_t qs = queue_pop (&ctx->jobs_queue, &job);
      if (qs == QS_INACTIVE)
        break;
      if (qs != QS_SUCCESS)
        {
          error ("Could not pop a job from reactor jobs queue");
          break;
        }

      status_t st = job.job_func (job.arg);

      if (job.cleanup)
        job.cleanup (job.arg);

      if (st == S_FAILURE)
        {
          error ("Could not complete reactor job");
          break;
        }
    }

  event_base_loopbreak (ctx->ev_base);
  return (NULL);
}

static void *
reactor_dispatch_event_loop (void *arg)
{
  reactor_context_t *ctx = *(reactor_context_t **)arg;

  if (event_base_dispatch (ctx->ev_base) != 0)
    error ("Could not dispatch the event loop");

  return (NULL);
}

status_t
reactor_context_init (reactor_context_t *ctx)
{
  ctx->shutting_down = false;
  ctx->ev_base = NULL;

  if (queue_init (&ctx->jobs_queue, sizeof (reactor_job_t)) != QS_SUCCESS)
    {
      error ("Could not initialize reactor jobs queue");
      return (S_FAILURE);
    }

  ctx->ev_base = event_base_new ();
  if (!ctx->ev_base)
    {
      error ("Could not initialize event base");
      if (queue_destroy (&ctx->jobs_queue) != QS_SUCCESS)
        error ("Could not destroy reactor jobs queue during cleanup");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
reactor_context_stop (reactor_context_t *ctx)
{
  ctx->shutting_down = true;

  if (ctx->ev_base)
    event_base_loopbreak (ctx->ev_base);

  if (queue_cancel (&ctx->jobs_queue) != QS_SUCCESS)
    {
      error ("Could not cancel reactor jobs queue");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
reactor_context_destroy (reactor_context_t *ctx)
{
  if (queue_drain (&ctx->jobs_queue, reactor_job_cleanup_cb, NULL)
      != QS_SUCCESS)
    error ("Could not drain reactor jobs queue");

  if (queue_destroy (&ctx->jobs_queue) != QS_SUCCESS)
    {
      error ("Could not destroy reactor jobs queue");
      return (S_FAILURE);
    }

  if (ctx->ev_base)
    {
      event_base_free (ctx->ev_base);
      ctx->ev_base = NULL;
    }

  return (S_SUCCESS);
}

queue_status_t
reactor_push_job (reactor_context_t *ctx, reactor_job_t *job)
{
  return (queue_push_back (&ctx->jobs_queue, job));
}

status_t
create_reactor_threads (thread_pool_t *thread_pool, long io_threads,
                        reactor_context_t *ctx)
{
  reactor_context_t *ctx_ptr = ctx;

  if (create_threads (thread_pool, io_threads, reactor_handle_io, &ctx_ptr,
                      sizeof (ctx_ptr), "i/o handler")
      == 0)
    {
      error ("Could not create reactor I/O threads");
      return (S_FAILURE);
    }

  if (!thread_create (thread_pool, reactor_dispatch_event_loop, &ctx_ptr,
                      sizeof (ctx_ptr), "event loop dispatcher"))
    {
      error ("Could not create event loop dispatcher thread");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
reactor_conn_init (reactor_conn_t *conn, reactor_context_t *rctr_ctx,
                   evutil_socket_t socket_fd,
                   void (*read_cb) (evutil_socket_t, short, void *), void *arg)
{
  memset (conn, 0, sizeof (*conn));

  if (pthread_mutex_init (&conn->mutex, NULL) != 0)
    {
      error ("Could not initialize reactor connection mutex");
      return (S_FAILURE);
    }

  conn->rctr_ctx = rctr_ctx;
  conn->socket_fd = socket_fd;
  conn->closing = false;
  conn->ref_count = 1;

  if (evutil_make_socket_nonblocking (conn->socket_fd) != 0)
    {
      error ("Could not change socket to nonblocking");
      pthread_mutex_destroy (&conn->mutex);
      return (S_FAILURE);
    }

  conn->read_event = event_new (rctr_ctx->ev_base, conn->socket_fd,
                                EV_READ | EV_PERSIST, read_cb, arg);
  if (!conn->read_event)
    {
      error ("Could not create read event");
      pthread_mutex_destroy (&conn->mutex);
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
reactor_conn_destroy (reactor_conn_t *conn)
{
  assert (conn->read_event == NULL);
  assert (atomic_load_explicit (&conn->ref_count, memory_order_relaxed) == 0);

  pthread_mutex_destroy (&conn->mutex);

  shutdown (conn->socket_fd, SHUT_RDWR);
  if (close (conn->socket_fd) != 0)
    {
      error ("Could not close connection socket");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

void
reactor_conn_close (reactor_conn_t *conn)
{
  pthread_mutex_lock (&conn->mutex);
  conn->closing = true;
  pthread_mutex_unlock (&conn->mutex);
}

bool
reactor_conn_is_closing (reactor_conn_t *conn)
{
  pthread_mutex_lock (&conn->mutex);
  bool is_closing = conn->closing;
  pthread_mutex_unlock (&conn->mutex);

  return (is_closing);
}

bool
reactor_conn_try_ref (reactor_conn_t *conn)
{
  pthread_mutex_lock (&conn->mutex);
  if (conn->closing)
    {
      pthread_mutex_unlock (&conn->mutex);
      return (false);
    }

  atomic_fetch_add_explicit (&conn->ref_count, 1, memory_order_relaxed);
  pthread_mutex_unlock (&conn->mutex);
  return (true);
}

bool
reactor_conn_unref (reactor_conn_t *conn)
{
  int old
      = atomic_fetch_sub_explicit (&conn->ref_count, 1, memory_order_acq_rel);
  assert (old > 0);

  return (old == 1);
}

bool
reactor_conn_release_event_ref (reactor_conn_t *conn)
{
  pthread_mutex_lock (&conn->mutex);
  struct event *ev = conn->read_event;
  conn->read_event = NULL;
  pthread_mutex_unlock (&conn->mutex);

  if (!ev)
    return (false);

  if (event_del (ev) == -1)
    error ("Could not delete read event");
  event_free (ev);

  return (reactor_conn_unref (conn));
}
