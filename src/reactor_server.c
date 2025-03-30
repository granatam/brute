#include "reactor_server.h"

#include "common.h"
#include "log.h"
#include "multi.h"
#include "queue.h"
#include "server_common.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <assert.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

static status_t
rsrv_ctx_init (rsrv_context_t *ctx)
{
  if (queue_init (&ctx->starving_clients, sizeof (cl_ctx_t *)) == QS_FAILURE)
    {
      error ("Could not initialize a starving clients queue");
      return (S_FAILURE);
    }
  if (queue_init (&ctx->jobs_queue, sizeof (job_t)) == QS_FAILURE)
    {
      error ("Could not initialize a jobs queue");
      return (S_FAILURE);
    }

  ctx->base = event_base_new ();
  if (!ctx->base)
    {
      error ("Could not initialize event base");
      return (S_FAILURE);
    }
  trace ("Allocated memory for event loop base");

  if (evutil_make_socket_nonblocking (ctx->context.socket_fd) < 0)
    {
      error ("Could not change socket to be nonblocking");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

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

static int
collect_events_cb (const struct event_base *base, const struct event *ev,
                   void *arg)
{
  (void)base; /* to suppress the "unused parameter" warning */

  event_list_t *list = arg;
  event_node_t *node = calloc (1, sizeof (*node));
  node->next = &list->head;
  node->prev = list->head.prev;
  node->ev = ev;

  list->head.prev->next = node;
  list->head.prev = node;

  return 0;
}

static void cl_ctx_destroy (cl_ctx_t *ctx);

static void
clients_cleanup (event_list_t *list)
{
  event_node_t *curr = list->head.next;
  while (curr->ev)
    {
      event_node_t *dummy = curr;
      cl_ctx_t *ctx = event_get_callback_arg (curr->ev);
      trace ("Destroying client context");
      cl_ctx_destroy (ctx);

      curr->prev->next = curr->next;
      curr->next->prev = curr->prev;
      curr = curr->next;
      free (dummy);
    }
}

static status_t
rsrv_context_destroy (rsrv_context_t *ctx)
{
  event_base_loopbreak (ctx->base);
  trace ("Stopped event loop");

  if (serv_base_context_destroy (&ctx->context) == S_FAILURE)
    {
      error ("Could not destroy server context");
      return (S_FAILURE);
    }
  trace ("Destroyed server context");

  if (queue_destroy (&ctx->jobs_queue) == QS_FAILURE)
    {
      error ("Could not destroy a jobs queue");
      return (S_FAILURE);
    }
  if (queue_destroy (&ctx->starving_clients) == QS_FAILURE)
    {
      error ("Could not destroy a starving clients queue");
      return (S_FAILURE);
    }
  trace ("Destroyed global server queues");

  event_list_t ev_list;
  ev_list.head.prev = &ev_list.head;
  ev_list.head.next = &ev_list.head;
  ev_list.head.ev = NULL;
  event_base_foreach_event (ctx->base, collect_events_cb, &ev_list);
  clients_cleanup (&ev_list);
  event_base_free (ctx->base);
  trace ("Deallocated clients contexts");

  return (S_SUCCESS);
}

static void handle_read (evutil_socket_t, short, void *);

static cl_ctx_t *
cl_ctx_init (rsrv_context_t *srv_ctx, evutil_socket_t fd)
{
  cl_ctx_t *cl_ctx = calloc (1, sizeof (*cl_ctx));
  if (!cl_ctx)
    {
      error ("Could not allocate client context");
      return (NULL);
    }
  trace ("Allocated memory for client context");
  cl_ctx->socket_fd = fd;
  cl_ctx->context = srv_ctx;
  cl_ctx->write_state = (client_state_t){
    .len = 0,
  };
  cl_ctx->read_state = (client_state_t){
    .len = 0,
    .buf = &cl_ctx->read_buffer,
  };
  cl_ctx->is_starving = false;

  pthread_mutex_init (&cl_ctx->is_writing_mutex, NULL);
  pthread_mutex_init (&cl_ctx->write_state_mutex, NULL);
  pthread_mutex_init (&cl_ctx->is_starving_mutex, NULL);
  pthread_mutex_init (&cl_ctx->registry_used_mutex, NULL);

  if (queue_init (&cl_ctx->registry_idx, sizeof (int)) != QS_SUCCESS)
    {
      error ("Could not initialize registry indices queue");
      goto fail;
    }
  for (int i = 0; i < QUEUE_SIZE; ++i)
    if (queue_push (&cl_ctx->registry_idx, &i) != QS_SUCCESS)
      {
        error ("Could not push index to registry indices queue");
        goto fail;
      }

  if (evutil_make_socket_nonblocking (cl_ctx->socket_fd) != 0)
    {
      error ("Could not change socket to be nonblocking");
      goto fail;
    }

  cl_ctx->read_event = event_new (srv_ctx->base, cl_ctx->socket_fd,
                                  EV_READ | EV_PERSIST, handle_read, cl_ctx);
  if (!cl_ctx->read_event)
    {
      error ("Could not create read event");
      goto fail;
    }

  return (cl_ctx);

fail:
  free (cl_ctx);
  return (NULL);
}

static void
cl_ctx_destroy (cl_ctx_t *ctx)
{
  pthread_mutex_destroy (&ctx->is_writing_mutex);
  pthread_mutex_destroy (&ctx->write_state_mutex);
  pthread_mutex_destroy (&ctx->is_starving_mutex);
  pthread_mutex_destroy (&ctx->registry_used_mutex);
  trace ("Destroyed client's mutexes");

  if (queue_destroy (&ctx->registry_idx) != QS_SUCCESS)
    {
      error ("Could not destroy registry indices queue");
    }
  trace ("Destroyed registry indices queue");

  shutdown (ctx->socket_fd, SHUT_RDWR);
  close (ctx->socket_fd);
  trace ("Closed client socket");

  if (event_del (ctx->read_event) == -1)
    {
      error ("Could not delete read event");
    }
  event_free (ctx->read_event);
  free (ctx);
  trace ("Deleted and deallocated event, free'd client context");
}

static status_t
push_job (cl_ctx_t *ctx, status_t (*job_func) (void *))
{
  job_t job = {
    .arg = ctx,
    .job_func = job_func,
  };
  if (queue_push_back (&ctx->context->jobs_queue, &job) != QS_SUCCESS)
    {
      error ("Could not push job to a job queue");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

static status_t create_task_job (void *);

static status_t
send_task_job (void *arg)
{
  if (!arg)
    return (S_FAILURE);
  cl_ctx_t *ctx = arg;

  command_t cmd = CMD_TASK;

  task_t task = *(task_t *)ctx->write_state.buf;
  pthread_mutex_unlock (&ctx->write_state_mutex);
  task.task.is_correct = false;
  struct iovec vec[] = { { .iov_base = &cmd, .iov_len = sizeof (cmd) },
                         { .iov_base = &task, .iov_len = sizeof (task) } };
  struct iovec *_vec = vec;
  int iovcnt = sizeof (vec) / sizeof (vec[0]);
  int expected_write = vec[0].iov_len + vec[1].iov_len;
  if (ctx->write_state.len > vec[0].iov_len)
    {
      _vec = &vec[1];
      iovcnt = 1;
      ctx->write_state.len -= vec[0].iov_len;
      _vec->iov_base += ctx->write_state.len;
      _vec->iov_len -= ctx->write_state.len;
      expected_write = _vec->iov_len;
    }
  else
    {
      vec[0].iov_base += ctx->write_state.len;
      vec[0].iov_len -= ctx->write_state.len;
      expected_write -= ctx->write_state.len;
    }

  int actual_write = 0;
  status_t status
      = send_wrapper_nonblock (ctx->socket_fd, _vec, iovcnt, &actual_write);
  if (status == S_FAILURE || actual_write != expected_write)
    {
      error ("Could not send task to client, expected count: %d, actual: %d",
             expected_write, actual_write);
      if (status == S_FAILURE)
        {
          cl_ctx_destroy (ctx);
          return (S_SUCCESS);
        }
      ctx->write_state.len += actual_write;
      return (push_job (ctx, send_task_job));
    }

  ctx->write_state.len = 0;

  trace ("%p: Sent task '%s' with id %d to client", ctx,
         task.task.password + task.to, task.task.id);

  pthread_mutex_lock (&ctx->is_writing_mutex);
  ctx->is_writing = false;
  pthread_mutex_unlock (&ctx->is_writing_mutex);

  return (push_job (ctx, create_task_job));
}

static status_t
create_task_job (void *arg)
{
  if (!arg)
    return (S_FAILURE);

  cl_ctx_t *ctx = arg;

  pthread_mutex_lock (&ctx->is_writing_mutex);
  if (ctx->is_writing)
    {
      pthread_mutex_unlock (&ctx->is_writing_mutex);
      return (S_SUCCESS);
    }
  ctx->is_writing = true;
  pthread_mutex_unlock (&ctx->is_writing_mutex);

  int id;
  queue_status_t qs;

  pthread_mutex_lock (&ctx->is_starving_mutex);
  bool is_starving = ctx->is_starving;
  pthread_mutex_unlock (&ctx->is_starving_mutex);
  if (is_starving)
    return (S_SUCCESS);

  qs = queue_trypop (&ctx->registry_idx, &id);
  if (qs != QS_SUCCESS)
    {
      pthread_mutex_lock (&ctx->is_writing_mutex);
      ctx->is_writing = false;
      pthread_mutex_unlock (&ctx->is_writing_mutex);
      return (qs == QS_EMPTY ? S_SUCCESS : S_FAILURE);
    }

  trace ("Got index %d from registry", id);

  task_t *task = &ctx->registry[id];
  mt_context_t *mt_ctx = &ctx->context->context.context;

  qs = queue_trypop (&mt_ctx->queue, task);
  if (qs == QS_EMPTY)
    {
      trace ("No tasks in global queue, add to starving clients");
      pthread_mutex_lock (&ctx->is_starving_mutex);
      ctx->is_starving = true;
      pthread_mutex_unlock (&ctx->is_starving_mutex);

      if (queue_push_back (&ctx->registry_idx, &id) == QS_FAILURE)
        {
          error ("Could not push index to registry indices queue");
          return (S_FAILURE);
        }
      trace ("Pushed id %d back to registry indices queue", id);
      if (queue_push_back (&ctx->context->starving_clients, &ctx)
          == QS_FAILURE)
        {
          error ("Could not push index to registry indices queue");
          return (S_FAILURE);
        }
      trace ("Pushed client context to starving clients queue");
      pthread_mutex_lock (&ctx->is_writing_mutex);
      ctx->is_writing = false;
      pthread_mutex_unlock (&ctx->is_writing_mutex);
      return (S_SUCCESS);
    }

  if (qs == QS_FAILURE)
    {
      if (queue_push_back (&ctx->registry_idx, &id) != QS_SUCCESS)
        {
          error ("Could not push back id to registry indices queue");
        }
      pthread_mutex_lock (&ctx->is_writing_mutex);
      ctx->is_writing = false;
      pthread_mutex_unlock (&ctx->is_writing_mutex);
      return (S_FAILURE);
    }

  trace ("Got task from global queue");

  pthread_mutex_lock (&ctx->registry_used_mutex);
  ctx->registry_used[id] = true;
  pthread_mutex_unlock (&ctx->registry_used_mutex);
  trace ("%p: Set id %d in registry_used as true", ctx, id);

  pthread_mutex_lock (&ctx->write_state_mutex);
  ctx->write_state.buf = task;
  task_t *buf = ctx->write_state.buf;
  buf->task.id = id;
  buf->to = task->from;
  buf->from = 0;

  return (push_job (ctx, send_task_job));
}

static status_t
send_config_job (void *arg)
{
  if (!arg)
    return (S_FAILURE);

  cl_ctx_t *ctx = arg;
  mt_context_t *mt_ctx = &ctx->context->context.context;

  // TODO: async write (send_task)
  // Impl abstract function that supports set of buffers (write_state, fixed
  // size = 2, pointer and len)
  if (send_config_data (ctx->socket_fd, mt_ctx) == S_FAILURE)
    return (S_FAILURE);

  return (push_job (ctx, create_task_job));
}

static void
handle_read (evutil_socket_t socket_fd, short what, void *arg)
{
  assert (what == EV_READ);
  /* We already have socket_fd in cl_ctx_t */
  (void)socket_fd; /* to suppress "unused parameter" warning */

  cl_ctx_t *ctx = arg;
  mt_context_t *mt_ctx = &ctx->context->context.context;

  char *buf = ((char *)&ctx->read_buffer) + ctx->read_state.len;
  ssize_t expected_read = sizeof (ctx->read_buffer) - ctx->read_state.len;
  int actual_read = 0;
  status_t status = recv_wrapper_nonblock (ctx->socket_fd, buf, expected_read,
                                           0, &actual_read);

  if (status == S_FAILURE || actual_read != expected_read)
    {
      if (actual_read == -1)
        {
          error ("Could not read result from a client");
          cl_ctx_destroy (ctx);
          return;
        }

      ctx->read_state.len += actual_read;
      return;
    }
  ctx->read_state.len = 0;

  result_t *result = &ctx->read_buffer;

  pthread_mutex_lock (&ctx->registry_used_mutex);
  bool is_used = ctx->registry_used[result->id];
  ctx->registry_used[result->id] = false;
  pthread_mutex_unlock (&ctx->registry_used_mutex);

  if (!is_used)
    {
      warn ("%p: Unexpected result id: %d", ctx, result->id);
      return;
    }

  if (queue_push_back (&ctx->registry_idx, &result->id) != QS_SUCCESS)
    {
      error ("Could not return id to a queue");
      return;
    }

  trace ("Pushed index %d back to indices queue", result->id);

  if (result->is_correct)
    {
      if (queue_cancel (&mt_ctx->queue) == QS_FAILURE)
        {
          error ("Could not cancel a queue");
          return;
        }
      memcpy (mt_ctx->password, result->password, sizeof (result->password));
      if (serv_signal_if_found (mt_ctx) == S_FAILURE)
        return;
      event_base_loopbreak (ctx->context->base);
    }

  trace ("Received %s result %s with id %d from client",
         result->is_correct ? "correct" : "incorrect", result->password,
         result->id);

  pthread_mutex_lock (&ctx->is_writing_mutex);
  bool is_writing = ctx->is_writing;
  pthread_mutex_unlock (&ctx->is_writing_mutex);

  if (!is_writing)
    {
      push_job (ctx, create_task_job);
      trace ("Pushed create task job from read event");
    }
}

static void *
handle_starving_clients (void *arg)
{
  rsrv_context_t *ctx = *(rsrv_context_t **)arg;

  for (;;)
    {
      cl_ctx_t *client = NULL;
      if (queue_pop (&ctx->starving_clients, &client) != QS_SUCCESS)
        {
          error ("Could not pop from a starving clients queue");
          return (NULL);
        }
      trace ("Got starving client from queue");

      int id;
      if (queue_pop (&client->registry_idx, &id) != QS_SUCCESS)
        {
          error ("Could not pop index from registry indices queue");
          return (NULL);
        }
      trace ("Got id for a starving client: %d", id);

      task_t task;
      if (queue_pop (&ctx->context.context.queue, &task) != QS_SUCCESS)
        {
          error ("Could not pop from the global queue");
          return (NULL);
        }
      trace ("Got task for a starving client");

      pthread_mutex_lock (&client->is_writing_mutex);
      client->is_writing = true;
      pthread_mutex_unlock (&client->is_writing_mutex);

      pthread_mutex_lock (&client->write_state_mutex);
      memcpy (client->write_state.buf, &task, sizeof (task));
      client->write_state.len = 0;
      task_t *buf = client->write_state.buf;
      buf->task.id = id;
      buf->to = buf->from;
      buf->from = 0;

      trace ("Set up starving client write state");

      pthread_mutex_lock (&client->registry_used_mutex);
      client->registry_used[id] = true;
      pthread_mutex_unlock (&client->registry_used_mutex);

      push_job (client, send_task_job);
      trace ("Pushed send job to jobs queue");

      pthread_mutex_lock (&client->is_starving_mutex);
      client->is_starving = false;
      pthread_mutex_unlock (&client->is_starving_mutex);
    }

  return (NULL);
}

static void *
handle_io (void *arg)
{
  rsrv_context_t *ctx = *(rsrv_context_t **)arg;

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

  event_base_loopbreak (ctx->base);
  return (NULL);
}

static void *
dispatch_event_loop (void *arg)
{
  rsrv_context_t *ctx = *(rsrv_context_t **)arg;
  if (event_base_dispatch (ctx->base) != 0)
    error ("Could not dispatch the event loop");

  return (NULL);
}

static void
handle_accept_error (struct evconnlistener *listener, void *arg)
{
  (void)listener;

  warn ("Got error on connection accept: %m");
  rsrv_context_t *ctx = arg;
  event_base_loopbreak (ctx->base);
}

static void
handle_accept (struct evconnlistener *listener, evutil_socket_t fd,
               struct sockaddr *address, int socklen, void *ctx)
{
  /* address and socklen are useless for this function, listener is needed only
   * for evconnlistener_get_base () call, but we do not use it, so it is also
   * useless */
  (void)listener; /* to suppress "unused parameter" warning */
  (void)address;  /* to suppress "unused parameter" warning */
  (void)socklen;  /* to suppress "unused parameter" warning */

  rsrv_context_t *srv_ctx = ctx;

  cl_ctx_t *cl_ctx = cl_ctx_init (srv_ctx, fd);
  if (!cl_ctx)
    goto fail;

  trace ("Got new connection and created new event");

  if (event_add (cl_ctx->read_event, NULL) != 0)
    {
      error ("Could not add event to event base");
      goto destroy_ctx;
    }

  trace ("Added new event");

  if (push_job (cl_ctx, send_config_job) == S_FAILURE)
    {
      error ("Could not add send_config job");
      goto destroy_ctx;
    }

  return;

destroy_ctx:
  cl_ctx_destroy (cl_ctx);
  cl_ctx = NULL;
  return;

fail:
  if (close_client (fd) == S_FAILURE)
    error ("Could not close client");
}

bool
run_reactor_server (task_t *task, config_t *config)
{
  signal (SIGPIPE, SIG_IGN);
  rsrv_context_t rsrv_ctx;
  rsrv_context_t *context_ptr = &rsrv_ctx;

  if (serv_base_context_init (&rsrv_ctx.context, config) == S_FAILURE)
    {
      error ("Could not initialize server context");
      serv_base_context_destroy (&rsrv_ctx.context);
      return (false);
    }

  if (rsrv_ctx_init (context_ptr) == S_FAILURE)
    goto fail;

  struct evconnlistener *listener
      = evconnlistener_new (rsrv_ctx.base, handle_accept, &rsrv_ctx,
                            LEV_OPT_REUSEABLE, -1, rsrv_ctx.context.socket_fd);
  if (!listener)
    {
      error ("Could not create a listener");
      goto fail;
    }
  evconnlistener_set_error_cb (listener, handle_accept_error);

  thread_pool_t *thread_pool = &rsrv_ctx.context.context.thread_pool;

  // TODO: define PTR_SIZE 8 to get rid of annoying clang-tidy warning?
  // Or maybe better solution will be to disable this warning in .clang-tidy
  // config file
  // Suspicious usage of sizeof() on an expression of pointer type
  int number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;
  if (create_threads (thread_pool, number_of_threads, handle_io, &context_ptr,
                      sizeof (context_ptr), "i/o handler")
      == 0)
    goto free_listener;
  trace ("Created I/O handler thread");

  if (!thread_create (thread_pool, handle_starving_clients, &context_ptr,
                      sizeof (context_ptr), "starving clients handler"))
    goto free_listener;
  trace ("Created starving clients handler thread");

  if (!thread_create (thread_pool, dispatch_event_loop, &context_ptr,
                      sizeof (context_ptr), "event loop dispatcher"))
    goto free_listener;
  trace ("Created event loop dispatcher thread");

  mt_context_t *mt_ctx = (mt_context_t *)&rsrv_ctx;
  if (process_tasks (task, config, mt_ctx) == S_FAILURE)
    goto free_listener;

  evconnlistener_free (listener);

  if (rsrv_context_destroy (&rsrv_ctx) == S_FAILURE)
    {
      error ("Could not destroy server context");
    }

  return (mt_ctx->password[0] != 0);

free_listener:
  if (listener)
    evconnlistener_free (listener);
fail:
  trace ("Failed, destroying server context");
  if (rsrv_context_destroy (&rsrv_ctx) == S_FAILURE)
    {
      error ("Could not destroy server context");
    }

  return (false);
}
