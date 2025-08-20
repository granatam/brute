#include "reactor_common.h"

#include "log.h"

#include <event2/event.h>
#include <string.h>
#include <unistd.h>

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

  event_base_loopbreak (conn->rctr_ctx->ev_base);

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
  reactor_context_t *ctx = arg;
  if (event_base_dispatch (ctx->ev_base) != 0)
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
      if (queue_pop (&ctx->jobs_queue, &job) != QS_SUCCESS)
        {
          error ("Could not pop a job from a job queue");
          break;
        }

      trace ("Got job from a job queue");

      if (job.job_func (job.arg) == S_FAILURE)
        {
          error ("Could not complete a job");
          break;
        }
    }

  trace ("After loop");
  event_base_loopbreak (ctx->ev_base);
  return (NULL);
}

status_t
write_state_write_wrapper (int socket_fd, struct iovec *vec, size_t *vec_sz)
{
  size_t actual_write = writev (socket_fd, vec, *vec_sz);

  if ((ssize_t)actual_write <= 0)
    {
      error ("Could not send data to client");
      return (S_FAILURE);
    }

  size_t i = 0;
  while (actual_write > 0 && vec[i].iov_len <= actual_write)
    actual_write -= vec[i++].iov_len;

  *vec_sz -= i;
  memmove (&vec[0], &vec[i], sizeof (struct iovec) * *vec_sz);

  vec[i].iov_base += actual_write;
  vec[i].iov_len -= actual_write;

  return (S_SUCCESS);
}

inline status_t
write_state_write (int socket_fd, write_state_t *write_state)
{
  return (write_state_write_wrapper (socket_fd, write_state->base_state.vec,
                                     &write_state->base_state.vec_sz));
}

status_t
push_job (reactor_context_t *rctr_ctx, void *arg,
          status_t (*job_func) (void *))
{
  job_t job = {
    .arg = arg,
    .job_func = job_func,
  };
  if (queue_push_back (&rctr_ctx->jobs_queue, &job) != QS_SUCCESS)
    {
      error ("Could not push job to a job queue");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
create_reactor_threads (thread_pool_t *tp, config_t *config,
                        reactor_context_t *ptr)
{
  int number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;
  if (create_threads (tp, 1, handle_io, &ptr, sizeof (ptr), "i/o handler")
      == 0)
    return (S_FAILURE);
  trace ("Created I/O handler thread");

  if (!thread_create (tp, dispatch_event_loop, ptr, sizeof (*ptr),
                      "event loop dispatcher"))
    return (S_FAILURE);

  trace ("Created event loop dispatcher thread");

  return (S_SUCCESS);
}
