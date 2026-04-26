#include "reactor_common.h"

#include "log.h"

#include <event2/event.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

status_t
reactor_conn_init (reactor_conn_t *conn, reactor_context_t *rctr_ctx,
                   evutil_socket_t fd, event_callback_fn on_read, void *arg)
{
  conn->rctr_ctx = rctr_ctx;
  conn->read_event = event_new (conn->rctr_ctx->ev_base, fd,
                                EV_READ | EV_PERSIST, on_read, arg);
  if (!conn->read_event)
    {
      error ("Could not create read event");
      return (S_FAILURE);
    }

  if (event_add (conn->read_event, NULL) != 0)
    {
      error ("Could not add event to event base");
      goto cleanup;
    }

  if (pthread_mutex_init (&conn->is_writing_mutex, NULL) != 0)
    {
      error ("Could not initialize mutex for write state");
      goto cleanup;
    }

  conn->is_writing = false;

  return (S_SUCCESS);

cleanup:
  event_free (conn->read_event);
  return (S_FAILURE);
}

status_t
reactor_conn_destroy (reactor_conn_t *conn, evutil_socket_t fd)
{
  pthread_mutex_destroy (&conn->is_writing_mutex);

  shutdown (fd, SHUT_RDWR);
  close (fd);

  if (event_del (conn->read_event) == -1)
    error ("Could not delete read event");
  event_free (conn->read_event);

  // event_base_loopbreak (conn->rctr_ctx->ev_base);

  return (S_SUCCESS);
}

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

void *
dispatch_event_loop (void *arg)
{
  trace ("dispatch_event_loop");
  reactor_context_t *ctx = arg;
  if (event_base_loop (ctx->ev_base, EVLOOP_NO_EXIT_ON_EMPTY) != 0)
    error ("Could not dispatch the event loop");

  return (NULL);
}

void *
handle_io (void *arg)
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

inline status_t
write_state_write (int socket_fd, write_state_t *write_state)
{
  return (write_state_write_wrapper (socket_fd, write_state->base_state.vec,
                                     &write_state->base_state.vec_sz));
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
  return queue_drain (&ctx->jobs_queue, reactor_job_drain_cb, NULL)
                 == QS_SUCCESS
             ? S_SUCCESS
             : S_FAILURE;
}

status_t
push_job (reactor_context_t *rctr_ctx, void *arg,
          status_t (*job_func) (void *), job_release_fn release_fn)
{
  job_t job = {
    .arg = arg,
    .job_func = job_func,
    .release_fn = release_fn,
  };

  if (queue_push_back (&rctr_ctx->jobs_queue, &job) != QS_SUCCESS)
    {
      error ("Could not push job to a job queue");
      return S_FAILURE;
    }

  return S_SUCCESS;
}

status_t
create_reactor_threads (thread_pool_t *tp, config_t *config,
                        reactor_context_t *ptr)
{
  long number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;
  if (create_threads (tp, number_of_threads, handle_io, &ptr, sizeof (ptr),
                      "i/o handler")
      == 0)
    return (S_FAILURE);
  trace ("Created I/O handler thread");

  if (!thread_create (tp, dispatch_event_loop, ptr, sizeof (*ptr),
                      "event loop dispatcher"))
    return (S_FAILURE);

  trace ("Created event loop dispatcher thread");

  return (S_SUCCESS);
}

static int
collect_events_cb (const struct event_base *ev_base, const struct event *ev,
                   void *arg)
{
  (void)ev_base;

  event_list_t *list = arg;
  event_node_t *node = calloc (1, sizeof (*node));
  if (!node)
    return 0;

  node->next = &list->head;
  node->prev = list->head.prev;
  node->ev = ev;

  list->head.prev->next = node;
  list->head.prev = node;

  return 0;
}

void
reactor_cleanup_clients (reactor_context_t *ctx,
                         event_callback_fn client_read_cb,
                         client_ctx_destroy_fn destroy_ctx)
{
  if (!ctx || !ctx->ev_base || !destroy_ctx)
    return;

  event_list_t ev_list;
  ev_list.head.prev = &ev_list.head;
  ev_list.head.next = &ev_list.head;
  ev_list.head.ev = NULL;

  event_base_foreach_event (ctx->ev_base, collect_events_cb, &ev_list);

  event_node_t *curr = ev_list.head.next;
  while (curr->ev)
    {
      event_node_t *dummy = curr;

      if (!client_read_cb || event_get_callback (curr->ev) == client_read_cb)
        {
          void *client_ctx = event_get_callback_arg (curr->ev);
          if (client_ctx)
            {
              trace ("Destroying client context from reactor_cleanup_clients");
              destroy_ctx (client_ctx);
            }
        }

      curr->prev->next = curr->next;
      curr->next->prev = curr->prev;
      curr = curr->next;
      free (dummy);
    }
}
