#include "reactor_client.h"

#include "client_common.h"
#include "common.h"
#include "log.h"
#include "queue.h"
#include "reactor_common.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <assert.h>
#include <event2/event.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* NOTE: io_state_t could be reused here. We might save first element in cmd
 * field and then we'd need only 2-element vector. */
typedef struct read_state_t
{
  struct iovec vec[3];
  size_t vec_sz;
  command_t cmd;
} read_state_t;

typedef struct client_context_t
{
  client_base_context_t client_base;
  thread_pool_t thread_pool;
  reactor_context_t rctr_ctx;
  pthread_mutex_t mutex;
  volatile bool done;
  pthread_cond_t cond_sem;
  struct event_base *ev_base;
  struct event *read_event;
  read_state_t read_state;
  /* Seems like we'll need a task queue and a result queue here. */
} client_context_t;

static void handle_read (evutil_socket_t, short, void *);

static status_t
client_context_init (client_context_t *ctx, config_t *config)
{
  memset (ctx, 0, sizeof (*ctx));

  if (thread_pool_init (&ctx->thread_pool) == S_FAILURE)
    {
      error ("Could not initialize thread pool");
      return (S_FAILURE);
    }
  if (queue_init (&ctx->rctr_ctx.jobs_queue, sizeof (job_t)) != QS_SUCCESS)
    {
      error ("Could not initialize jobs queue");
      return (S_FAILURE);
    }
  if (pthread_mutex_init (&ctx->mutex, NULL) != 0)
    {
      error ("Could not initialize mutex");
      goto cleanup;
    }
  if (pthread_cond_init (&ctx->cond_sem, NULL) != 0)
    {
      error ("Could not initialize conditional semaphore");
      goto cleanup;
    }

  ctx->done = false;

  ctx->rctr_ctx.ev_base = event_base_new ();
  if (!ctx->rctr_ctx.ev_base)
    {
      error ("Could not initialize event base");
      return (S_FAILURE);
    }
  trace ("Allocated memory for event loop base");

  if (client_base_context_init (&ctx->client_base, config, NULL) == S_FAILURE)
    {
      error ("Could not initialize client base context");
      goto cleanup;
    }

  ctx->read_event
      = event_new (ctx->rctr_ctx.ev_base, ctx->client_base.socket_fd,
                   EV_READ | EV_PERSIST, handle_read, ctx);
  if (!ctx->read_event)
    {
      error ("Could not create read event");
      goto cleanup;
    }

  return (S_SUCCESS);

cleanup:
  queue_destroy (&ctx->rctr_ctx.jobs_queue);

  client_base_context_destroy (&ctx->client_base);

  return (S_FAILURE);
}

static status_t
client_context_destroy (client_context_t *ctx)
{
  status_t status = S_SUCCESS;

  event_base_free (ctx->rctr_ctx.ev_base);
  if (event_del (ctx->read_event) == -1)
    error ("Could not delete read event");
  event_free (ctx->read_event);

  if (queue_cancel (&ctx->rctr_ctx.jobs_queue) != QS_SUCCESS)
    {
      error ("Could not cancel jobs queue");
      status = S_FAILURE;
      goto cleanup;
    }
  if (thread_pool_join (&ctx->thread_pool) == S_FAILURE)
    {
      error ("Could not cancel thread pool");
      status = S_FAILURE;
      goto cleanup;
    }

  trace ("Waited for all threads to end, closing the connection now");

cleanup:
  queue_destroy (&ctx->rctr_ctx.jobs_queue);

  client_base_context_destroy (&ctx->client_base);

  return (status);
}

static status_t
send_task_job (void *arg)
{
  (void)arg;
  /* 1. Try to pop a result from a result queue.
   * 2. If a result queue is empty, something wrong happened, because amount
   *    of send_task jobs must be equal to amount of processed tasks.
   * 3. Send task to the server. It'd be cool to reuse `send_task_job` from
   *    reactor server here. `write_state_t`?
   */

  return (S_SUCCESS);
}

static status_t
process_task_job (void *arg)
{
  (void)arg;
  /* 1. Initialize st_context.
   * 2. Call brute.
   * 3. Save result to a result queue.
   * 4. Push send_task_job to a job queue.
   */

  return (S_SUCCESS);
}

static void
handle_read (evutil_socket_t socket_fd, short what, void *arg)
{
  assert (what == EV_READ);
  /* We already have socket_fd in client_context_t */
  (void)socket_fd; /* to suppress "unused parameter" warning */

  client_context_t *ctx = arg;

  /* 1. Read the first element of vector from ctx->client_base.socket_fd.
   * 2. Convert data to command_t and if it is an alphabet, read other 2
   *    elements, else read just one.
   * 3. Based on a command, do the following:
   *    - If we've received an alphabet, set config->alph.
   *    - If this is a hash, set config->hash.
   *    - Otherwise, add task to a task queue and push a process_task job to
   *      a jobs queue.
   *
   * We should use io_state with three elements because we could receive either
   * 2 elements or 3 elements, and we should handle all possible cases.
   */
}

bool
run_reactor_client (config_t *config)
{
  client_context_t ctx;

  if (client_context_init (&ctx, config) == S_FAILURE)
    return (false);

  if (srv_connect (&ctx.client_base) == S_FAILURE)
    goto cleanup;

  reactor_context_t *rctr_ctx_ptr = &ctx.rctr_ctx;

  int number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;
  if (create_threads (&ctx.thread_pool, number_of_threads, handle_io,
                      &rctr_ctx_ptr, sizeof (rctr_ctx_ptr), "i/o handler")
      == 0)
    goto cleanup;
  trace ("Created I/O handler thread");

  if (!thread_create (&ctx.thread_pool, dispatch_event_loop, &ctx.rctr_ctx,
                      sizeof (ctx.rctr_ctx), "event loop dispatcher"))
    goto cleanup;
  trace ("Created event loop dispatcher thread");

  if (pthread_mutex_lock (&ctx.mutex) != 0)
    {
      error ("Could not lock a mutex");
      goto cleanup;
    }
  status_t status = S_SUCCESS;

  while (!ctx.done)
    if (pthread_cond_wait (&ctx.cond_sem, &ctx.mutex) != 0)
      {
        error ("Could not wait on a condition");
        status = S_FAILURE;
        break;
      }

  if (pthread_mutex_unlock (&ctx.mutex) != 0)
    {
      error ("Could not unlock a mutex");
      goto cleanup;
    }

  if (status != S_FAILURE)
    trace ("Got signal on conditional semaphore");

cleanup:
  if (client_context_destroy (&ctx) == S_FAILURE)
    error ("Could not destroy asynchronous client context");

  return (false);
}
