#include "reactor_common.h"

#include "log.h"

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
  // if (reactor_context_stop (ctx) != S_SUCCESS)
  //   return (S_FAILURE);
  //
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
