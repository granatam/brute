#include "reactor_common.h"

#include "log.h"

#include <event2/event.h>
#include <string.h>

void *
dispatch_event_loop (void *arg)
{
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

  event_base_loopbreak (ctx->ev_base);
  return (NULL);
}

status_t
write_state_write_wrapper (int socket_fd, struct iovec *vec, size_t *vec_sz)
{
  size_t actual_write = writev (socket_fd, vec, *vec_sz);

  if ((ssize_t)actual_write <= 0)
    {
      error ("Could not send config data to client");
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
