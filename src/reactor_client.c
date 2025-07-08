#include "reactor_client.h"

#include "client_common.h"
#include "common.h"
#include "log.h"
#include "queue.h"
#include "reactor_common.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <event2/event.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <sys/socket.h>
#include <sys/types.h>
#endif

typedef struct client_context_t
{
  client_base_context_t client_base;
  thread_pool_t thread_pool;
  queue_t jobs_queue;
  pthread_mutex_t mutex;
  volatile bool done;
  pthread_cond_t cond_sem;
  struct event_base *ev_base;
} client_context_t;

static status_t
client_context_init (client_context_t *ctx, config_t *config)
{
  memset (ctx, 0, sizeof (*ctx));

  if (thread_pool_init (&ctx->thread_pool) == S_FAILURE)
    {
      error ("Could not initialize thread pool");
      return (S_FAILURE);
    }
  if (queue_init (&ctx->jobs_queue, sizeof (job_t)) != QS_SUCCESS)
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

  if (client_base_context_init (&ctx->client_base, config, NULL) == S_FAILURE)
    {
      error ("Could not initialize client base context");
      goto cleanup;
    }

  return (S_SUCCESS);

cleanup:
  queue_destroy (&ctx->jobs_queue);

  client_base_context_destroy (&ctx->client_base);

  return (S_FAILURE);
}

static status_t
client_context_destroy (client_context_t *ctx)
{
  status_t status = S_SUCCESS;

  if (queue_cancel (&ctx->jobs_queue) != QS_SUCCESS)
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
  queue_destroy (&ctx->jobs_queue);

  client_base_context_destroy (&ctx->client_base);

  return (status);
}

static void *
dispatch_event_loop (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;
  if (event_base_dispatch (ctx->ev_base) != 0)
    error ("Could not dispatch the event loop");

  return (NULL);
}

bool
run_reactor_client (config_t *config)
{
  client_context_t ctx;

  if (client_context_init (&ctx, config) == S_FAILURE)
    return (false);

  if (srv_connect (&ctx.client_base) == S_FAILURE)
    goto cleanup;

  client_context_t *ctx_ptr = &ctx;

  int number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;
  // if (create_threads (&ctx.thread_pool, number_of_threads, handle_io,
  // &ctx_ptr,
  //                     sizeof (ctx_ptr), "i/o handler")
  //     == 0)
  //   goto cleanup;
  trace ("Created I/O handler thread");

  if (!thread_create (&ctx.thread_pool, dispatch_event_loop, &ctx_ptr,
                      sizeof (ctx_ptr), "event loop dispatcher"))
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

  if (S_FAILURE == status)
    goto cleanup;

  trace ("Got signal on conditional semaphore");

cleanup:
  if (client_context_destroy (&ctx) == S_FAILURE)
    error ("Could not destroy asynchronous client context");

  return (false);
}
