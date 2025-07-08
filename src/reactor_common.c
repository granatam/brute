#include "reactor_common.h"

#include "log.h"

#include <event2/event.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

status_t
reactor_context_init (reactor_context_t *ctx)
{
  if (queue_init (&ctx->jobs_queue, sizeof (job_t)) != QS_SUCCESS)
    {
      error ("Could not initialize jobs queue");
      return (S_FAILURE);
    }
  ctx->ev_base = event_base_new ();
  if (!ctx->ev_base)
    {
      error ("Could not initialize event base");
      queue_destroy (&ctx->jobs_queue);
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

void
reactor_context_stop (reactor_context_t *ctx)
{
  if (!ctx)
    return;

  if (ctx->ev_base)
    event_base_loopbreak (ctx->ev_base);

  if (queue_cancel (&ctx->jobs_queue) == QS_FAILURE)
    error ("Could not cancel reactor jobs queue");
}

status_t
reactor_context_destroy (reactor_context_t *ctx)
{
  if (queue_destroy (&ctx->jobs_queue) != QS_SUCCESS)
    {
      error ("Could not destroy jobs queue");
      return (S_FAILURE);
    }
  event_base_free (ctx->ev_base);

  return (S_SUCCESS);
}

static void
reactor_job_drain_cb (void *payload, void *arg)
{
  (void)arg;

  job_t *job = payload;
  if (job->release_fn)
    job->release_fn (job->arg);
}

status_t
reactor_context_drain_jobs (reactor_context_t *ctx)
{
  return (queue_drain (&ctx->jobs_queue, reactor_job_drain_cb, NULL)
                  == QS_SUCCESS
              ? S_SUCCESS
              : S_FAILURE);
}

reactor_push_status_t
reactor_push_job (reactor_context_t *rctr_ctx, void *arg,
                  status_t (*job_func) (void *), job_release_fn release_fn)
{
  job_t job = {
    .arg = arg,
    .job_func = job_func,
    .release_fn = release_fn,
  };

  queue_status_t qs = queue_push_back (&rctr_ctx->jobs_queue, &job);
  if (qs == QS_SUCCESS)
    return (RPS_SUCCESS);

  if (qs == QS_INACTIVE)
    return (RPS_INACTIVE);

  error ("Could not push job to a job queue");
  return (RPS_FAILURE);
}

typedef struct reactor_event_node_t
{
  const struct event *ev;
  struct reactor_event_node_t *next;
  struct reactor_event_node_t *prev;
} reactor_event_node_t;

typedef struct reactor_event_list_t
{
  reactor_event_node_t head;
} reactor_event_list_t;

static int
reactor_collect_events_cb (const struct event_base *ev_base,
                           const struct event *ev, void *arg)
{
  (void)ev_base;

  reactor_event_list_t *list = arg;
  reactor_event_node_t *node = calloc (1, sizeof (*node));
  if (!node)
    return (-1);

  node->next = &list->head;
  node->prev = list->head.prev;
  node->ev = ev;

  list->head.prev->next = node;
  list->head.prev = node;

  return (0);
}

status_t
reactor_for_each_event_snapshot (reactor_context_t *ctx,
                                 reactor_event_visit_fn visit, void *arg)
{
  if (!ctx || !ctx->ev_base || !visit)
    return (S_FAILURE);

  reactor_event_list_t list;
  list.head.prev = &list.head;
  list.head.next = &list.head;
  list.head.ev = NULL;

  if (event_base_foreach_event (ctx->ev_base, reactor_collect_events_cb, &list)
      != 0)
    return (S_FAILURE);

  reactor_event_node_t *curr = list.head.next;
  while (curr->ev)
    {
      reactor_event_node_t *next = curr->next;
      visit (curr->ev, arg);
      free (curr);
      curr = next;
    }

  return (S_SUCCESS);
}

status_t
reactor_event_del_free (struct event *ev)
{
  if (!ev)
    return (S_SUCCESS);

  status_t status = S_SUCCESS;

  if (event_del (ev) == -1)
    {
      error ("Could not delete event");
      status = S_FAILURE;
    }

  event_free (ev);
  return (status);
}

void *
reactor_event_loop (void *arg)
{
  reactor_context_t *ctx = arg;
  if (event_base_loop (ctx->ev_base, EVLOOP_NO_EXIT_ON_EMPTY) != 0)
    error ("Could not dispatch the event loop");

  return (NULL);
}

void *
reactor_worker_loop (void *arg)
{
  reactor_context_t *ctx = *(reactor_context_t **)arg;

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

      trace ("Got job from a job queue");

      status_t st = job.job_func (job.arg);

      if (job.release_fn)
        job.release_fn (job.arg);

      if (st == S_FAILURE)
        {
          error ("Could not complete a job");
          break;
        }
    }

  event_base_loopbreak (ctx->ev_base);
  return (NULL);
}

status_t
reactor_create_threads (thread_pool_t *tp, config_t *config,
                        reactor_context_t *ptr)
{
  long number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;
  if (create_threads (tp, number_of_threads, reactor_worker_loop, &ptr,
                      sizeof (ptr), "i/o handler")
      == 0)
    return (S_FAILURE);
  trace ("Created I/O handler thread");

  if (!thread_create (tp, reactor_event_loop, ptr, sizeof (*ptr),
                      "event loop dispatcher"))
    return (S_FAILURE);

  trace ("Created event loop dispatcher thread");

  return (S_SUCCESS);
}

status_t
reactor_conn_init (reactor_conn_t *conn, reactor_context_t *rctr_ctx,
                   evutil_socket_t fd, event_callback_fn on_read, void *arg)
{
  memset (conn, 0, sizeof (*conn));

  conn->fd = fd;

  if (evutil_make_socket_nonblocking (conn->fd) != 0)
    {
      error ("Could not change socket to be nonblocking");
      return (S_FAILURE);
    }

  conn->read_event
      = event_new (rctr_ctx->ev_base, fd, EV_READ | EV_PERSIST, on_read, arg);
  if (!conn->read_event)
    {
      error ("Could not create read event");
      conn->fd = -1;
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
reactor_conn_enable_read (reactor_conn_t *conn)
{
  if (!conn || !conn->read_event)
    return (S_FAILURE);

  if (event_add (conn->read_event, NULL) != 0)
    {
      error ("Could not add read event to event base");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
reactor_conn_disable_read (reactor_conn_t *conn)
{
  if (!conn || !conn->read_event)
    return (S_SUCCESS);

  if (event_del (conn->read_event) == -1)
    {
      error ("Could not delete read event");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
reactor_conn_destroy (reactor_conn_t *conn)
{
  if (!conn)
    return (S_SUCCESS);

  status_t status = S_SUCCESS;

  if (conn->read_event)
    {
      if (event_del (conn->read_event) == -1)
        {
          error ("Could not delete read event");
          status = S_FAILURE;
        }

      event_free (conn->read_event);
      conn->read_event = NULL;
    }

  if (conn->fd >= 0)
    {
      shutdown (conn->fd, SHUT_RDWR);

      if (close (conn->fd) != 0)
        {
          error ("Could not close socket");
          status = S_FAILURE;
        }

      conn->fd = -1;
    }

  return (status);
}

static void
reactor_iov_advance (struct iovec *vec, int *vec_sz, size_t bytes)
{
  int i = 0;

  while (i < *vec_sz && bytes > 0 && vec[i].iov_len <= bytes)
    {
      bytes -= vec[i].iov_len;
      ++i;
    }

  *vec_sz -= i;
  memmove (&vec[0], &vec[i], sizeof (struct iovec) * (size_t)*vec_sz);

  if (*vec_sz > 0 && bytes > 0)
    {
      vec[0].iov_base = (char *)vec[0].iov_base + bytes;
      vec[0].iov_len -= bytes;
    }
}

static reactor_io_status_t
reactor_writev_advance (int fd, struct iovec *vec, int *vec_sz)
{
  ssize_t written = writev (fd, vec, *vec_sz);

  if (written < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return (RIO_PENDING);

      return (RIO_ERROR);
    }

  if (written == 0)
    return (RIO_CLOSED);

  reactor_iov_advance (vec, vec_sz, (size_t)written);

  return (*vec_sz == 0 ? RIO_DONE : RIO_PENDING);
}

static reactor_io_status_t
reactor_readv_advance (int fd, struct iovec *vec, int *vec_sz)
{
  ssize_t nread = readv (fd, vec, *vec_sz);

  if (nread < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return (RIO_PENDING);

      return (RIO_ERROR);
    }

  if (nread == 0)
    return (RIO_CLOSED);

  reactor_iov_advance (vec, vec_sz, (size_t)nread);

  return (*vec_sz == 0 ? RIO_DONE : RIO_PENDING);
}

reactor_io_status_t
reactor_conn_readv (reactor_conn_t *conn, struct iovec *vec, int *vec_sz)
{
  if (!conn || conn->fd < 0)
    return (RIO_CLOSED);

  return (reactor_readv_advance (conn->fd, vec, vec_sz));
}

status_t
write_state_write_wrapper (int socket_fd, struct iovec *vec, int *vec_sz)
{
  reactor_io_status_t status = reactor_writev_advance (socket_fd, vec, vec_sz);

  if (status == RIO_ERROR || status == RIO_CLOSED)
    {
      error ("Could not send data");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

inline status_t
write_state_write (int socket_fd, write_state_t *write_state)
{
  return (write_state_write_wrapper (socket_fd, write_state->base_state.vec,
                                     &write_state->base_state.vec_sz));
}
