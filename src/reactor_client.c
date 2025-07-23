#include "reactor_client.h"

#include "brute.h"
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
  bool is_writing;
  pthread_mutex_t is_writing_mutex;
  write_state_t write_state;
  queue_t task_queue;
  queue_t result_queue;
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
  if (queue_init (&ctx->task_queue, sizeof (task_t)) != QS_SUCCESS)
    {
      error ("Could not initialize task queue");
      return (S_FAILURE);
    }
  if (queue_init (&ctx->result_queue, sizeof (result_t)) != QS_SUCCESS)
    {
      error ("Could not initialize result queue");
      queue_destroy (&ctx->task_queue);
      return (S_FAILURE);
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
  if (queue_cancel (&ctx->task_queue) != QS_SUCCESS)
    {
      error ("Could not cancel task queue");
      status = S_FAILURE;
      goto cleanup;
    }
  if (queue_cancel (&ctx->result_queue) != QS_SUCCESS)
    {
      error ("Could not cancel result queue");
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
  queue_destroy (&ctx->task_queue);
  queue_destroy (&ctx->result_queue);

  client_base_context_destroy (&ctx->client_base);

  return (status);
}

static status_t
send_result_job (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;

  result_t result;
  if (queue_pop (&ctx->result_queue, &result) != QS_SUCCESS)
    return (S_FAILURE);
  trace ("Got new result from result queue");

  status_t status
      = write_state_write (ctx->client_base.socket_fd, &ctx->write_state);
  if (status != S_SUCCESS)
    {
      error ("Could not send task to client");
      client_context_destroy (ctx);
      return (S_SUCCESS);
    }

  if (ctx->write_state.base_state.vec_sz != 0)
    return (push_job (&ctx->rctr_ctx, ctx, send_result_job));

  trace ("Sent task to client");

  pthread_mutex_lock (&ctx->is_writing_mutex);
  ctx->is_writing = false;
  pthread_mutex_unlock (&ctx->is_writing_mutex);
  trace ("Sent %s result %s to server",
         result.is_correct ? "correct" : "incorrect", result.password);

  return (S_SUCCESS);
}

static status_t
process_task_job (void *arg)
{
  client_context_t *ctx = arg;

  pthread_mutex_lock (&ctx->is_writing_mutex);
  if (ctx->is_writing)
    {
      pthread_mutex_unlock (&ctx->is_writing_mutex);
      return (S_SUCCESS);
    }
  ctx->is_writing = true;
  pthread_mutex_unlock (&ctx->is_writing_mutex);

  st_context_t st_context = {
    .hash = ctx->client_base.config->hash,
    .data = { .initialized = 0 },
  };

  queue_status_t qs;

  /* is_starving analogue? */

  /* Call trypop, if queue is empty then something is wrong */
  task_t task;

  task.result.is_correct = brute (&task, ctx->client_base.config,
                                  st_password_check, &st_context);
  trace ("Processed task");

  if (queue_push_back (&ctx->result_queue, &task.result) != QS_SUCCESS)
    return (S_FAILURE);

  /* Setup write state? */

  return (push_job (&ctx->rctr_ctx, ctx, send_result_job));
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

  // What should be the argument here?
  // if (!thread_create (&ctx.thread_pool, handle_waiting_tasks, &context_ptr,
  //                     sizeof (context_ptr), "waiting tasks handler"))
  //   goto cleanup;
  // trace ("Created waiting tasks handler thread");

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
