#include "reactor_common.h"

#include "log.h"

#include <event2/event.h>

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

  event_base_loopbreak (ctx->ev_base);
  return (NULL);
}
