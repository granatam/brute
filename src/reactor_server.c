#include "reactor_server.h"

#include "common.h"
#include "log.h"
#include "multi.h"
#include "queue.h"
#include "reactor_common.h"
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
  if (queue_init (&ctx->starving_clients, sizeof (client_context_t *))
      == QS_FAILURE)
    {
      error ("Could not initialize a starving clients queue");
      return (S_FAILURE);
    }
  if (queue_init (&ctx->rctr_ctx.jobs_queue, sizeof (job_t)) == QS_FAILURE)
    {
      error ("Could not initialize a jobs queue");
      return (S_FAILURE);
    }

  ctx->rctr_ctx.ev_base = event_base_new ();
  if (!ctx->rctr_ctx.ev_base)
    {
      error ("Could not initialize event base");
      return (S_FAILURE);
    }
  trace ("Allocated memory for event loop base");

  if (evutil_make_socket_nonblocking (ctx->srv_base.listen_fd) < 0)
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
collect_events_cb (const struct event_base *ev_base, const struct event *ev,
                   void *arg)
{
  (void)ev_base; /* to suppress the "unused parameter" warning */

  event_list_t *list = arg;
  event_node_t *node = calloc (1, sizeof (*node));
  node->next = &list->head;
  node->prev = list->head.prev;
  node->ev = ev;

  list->head.prev->next = node;
  list->head.prev = node;

  return 0;
}

static void client_context_destroy (client_context_t *ctx);

static void
clients_cleanup (event_list_t *list)
{
  event_node_t *curr = list->head.next;
  while (curr->ev)
    {
      event_node_t *dummy = curr;
      client_context_t *ctx = event_get_callback_arg (curr->ev);
      trace ("Destroying client context");
      client_context_destroy (ctx);

      curr->prev->next = curr->next;
      curr->next->prev = curr->prev;
      curr = curr->next;
      free (dummy);
    }
}

static status_t
rsrv_context_destroy (rsrv_context_t *ctx)
{
  event_base_loopbreak (ctx->rctr_ctx.ev_base);
  trace ("Stopped event loop");

  if (srv_base_context_destroy (&ctx->srv_base) == S_FAILURE)
    {
      error ("Could not destroy server context");
      return (S_FAILURE);
    }
  trace ("Destroyed server context");

  if (queue_destroy (&ctx->rctr_ctx.jobs_queue) == QS_FAILURE)
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
  event_base_foreach_event (ctx->rctr_ctx.ev_base, collect_events_cb,
                            &ev_list);
  clients_cleanup (&ev_list);
  event_base_free (ctx->rctr_ctx.ev_base);
  trace ("Deallocated clients contexts");

  return (S_SUCCESS);
}

static void handle_read (evutil_socket_t, short, void *);

static client_context_t *
client_context_init (rsrv_context_t *rsrv_ctx, evutil_socket_t fd)
{
  client_context_t *client_ctx = calloc (1, sizeof (*client_ctx));
  if (!client_ctx)
    {
      error ("Could not allocate client context");
      return (NULL);
    }
  trace ("Allocated memory for client context");

  client_ctx->socket_fd = fd;
  client_ctx->rsrv_ctx = rsrv_ctx;
  client_ctx->read_state.vec[0].iov_base = &client_ctx->read_buffer;
  client_ctx->read_state.vec[0].iov_len = sizeof (client_ctx->read_buffer);
  client_ctx->read_state.vec_sz = 1;

  mt_context_t *mt_ctx = &client_ctx->rsrv_ctx->srv_base.mt_ctx;

  write_state_t *write_state = &client_ctx->write_state;
  io_state_t *write_state_base = &write_state->base_state;

  /* Set up vector for hash sending */
  write_state_base->cmd = CMD_HASH;
  write_state_base->vec[0].iov_base = &write_state_base->cmd;
  write_state_base->vec[0].iov_len = sizeof (write_state_base->cmd);
  write_state_base->vec[1].iov_base = mt_ctx->config->hash;
  write_state_base->vec[1].iov_len = HASH_LENGTH;
  write_state_base->vec_sz = 2;

  /* Set up vector for alphabet sending */
  write_state->cmd_extra = CMD_ALPH;
  write_state->length = strlen (mt_ctx->config->alph);
  write_state->vec_extra[0].iov_base = &write_state->cmd_extra;
  write_state->vec_extra[0].iov_len = sizeof (write_state->cmd_extra);
  write_state->vec_extra[1].iov_base = &write_state->length;
  write_state->vec_extra[1].iov_len = sizeof (write_state->length);
  write_state->vec_extra[2].iov_base = mt_ctx->config->alph;
  write_state->vec_extra[2].iov_len = write_state->length;
  write_state->vec_extra_sz = 3;

  client_ctx->is_starving = false;

  pthread_mutex_init (&client_ctx->is_writing_mutex, NULL);
  pthread_mutex_init (&client_ctx->is_starving_mutex, NULL);
  pthread_mutex_init (&client_ctx->registry_used_mutex, NULL);

  if (queue_init (&client_ctx->registry_idx, sizeof (int)) != QS_SUCCESS)
    {
      error ("Could not initialize registry indices queue");
      goto fail;
    }
  for (int i = 0; i < QUEUE_SIZE; ++i)
    if (queue_push (&client_ctx->registry_idx, &i) != QS_SUCCESS)
      {
        error ("Could not push index to registry indices queue");
        goto fail;
      }

  if (evutil_make_socket_nonblocking (client_ctx->socket_fd) != 0)
    {
      error ("Could not change socket to be nonblocking");
      goto fail;
    }

  client_ctx->read_event
      = event_new (rsrv_ctx->rctr_ctx.ev_base, client_ctx->socket_fd,
                   EV_READ | EV_PERSIST, handle_read, client_ctx);
  if (!client_ctx->read_event)
    {
      error ("Could not create read event");
      goto fail;
    }

  return (client_ctx);

fail:
  free (client_ctx);
  return (NULL);
}

static status_t
return_tasks (client_context_t *ctx)
{
  mt_context_t *mt_ctx = &ctx->rsrv_ctx->srv_base.mt_ctx;
  status_t status = S_SUCCESS;

  int i;
  for (i = 0; i < QUEUE_SIZE; ++i)
    {
      if (ctx->registry_used[i])
        {
          if (queue_push_back (&mt_ctx->queue, &ctx->registry[i])
              != QS_SUCCESS)
            {
              status = S_FAILURE;
              break;
            }
          ctx->registry_used[i] = false;
        }
    }

  return (status);
}

static void
client_context_destroy (client_context_t *ctx)
{
  if (return_tasks (ctx) == S_FAILURE)
    error ("Could not return tasks to global queue");

  pthread_mutex_destroy (&ctx->is_writing_mutex);
  pthread_mutex_destroy (&ctx->is_starving_mutex);
  pthread_mutex_destroy (&ctx->registry_used_mutex);
  trace ("Destroyed client's mutexes");

  if (queue_destroy (&ctx->registry_idx) != QS_SUCCESS)
    error ("Could not destroy registry indices queue");

  trace ("Destroyed registry indices queue");

  shutdown (ctx->socket_fd, SHUT_RDWR);
  close (ctx->socket_fd);
  trace ("Closed client socket");

  if (event_del (ctx->read_event) == -1)
    error ("Could not delete read event");
  event_free (ctx->read_event);
  free (ctx);
  trace ("Deleted and deallocated event, free'd client context");
}

static status_t
push_job (client_context_t *ctx, status_t (*job_func) (void *))
{
  job_t job = {
    .arg = ctx,
    .job_func = job_func,
  };
  if (queue_push_back (&ctx->rsrv_ctx->rctr_ctx.jobs_queue, &job)
      != QS_SUCCESS)
    {
      error ("Could not push job to a job queue");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

static status_t create_task_job (void *);

static status_t
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

static inline status_t
write_state_write (int socket_fd, write_state_t *write_state)
{
  return (write_state_write_wrapper (socket_fd, write_state->base_state.vec,
                                     &write_state->base_state.vec_sz));
}

static inline status_t
write_state_write_extra (int socket_fd, write_state_t *write_state)
{
  return (write_state_write_wrapper (socket_fd, write_state->vec_extra,
                                     &write_state->vec_extra_sz));
}

static status_t
send_task_job (void *arg)
{
  client_context_t *ctx = arg;

  status_t status = write_state_write (ctx->socket_fd, &ctx->write_state);
  if (status != S_SUCCESS)
    {
      error ("Could not send task to client");
      client_context_destroy (ctx);
      return (S_SUCCESS);
    }

  if (ctx->write_state.base_state.vec_sz != 0)
    return (push_job (ctx, send_task_job));

  trace ("Sent task to client");

  pthread_mutex_lock (&ctx->is_writing_mutex);
  ctx->is_writing = false;
  pthread_mutex_unlock (&ctx->is_writing_mutex);

  return (push_job (ctx, create_task_job));
}

static void
task_write_state_setup (client_context_t *ctx, int id)
{
  task_t *task = &ctx->registry[id];
  task->result.id = id;
  task->to = task->from;
  task->from = 0;
  task->result.is_correct = false;
  io_state_t *write_state_base = &ctx->write_state.base_state;
  write_state_base->cmd = CMD_TASK;
  write_state_base->vec[0].iov_base = &write_state_base->cmd;
  write_state_base->vec[0].iov_len = sizeof (write_state_base->cmd);
  write_state_base->vec[1].iov_base = task;
  write_state_base->vec[1].iov_len = sizeof (*task);
  write_state_base->vec_sz = 2;
}

static status_t
create_task_job (void *arg)
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
  mt_context_t *mt_ctx = &ctx->rsrv_ctx->srv_base.mt_ctx;

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
      if (queue_push_back (&ctx->rsrv_ctx->starving_clients, &ctx)
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
        error ("Could not push back id to registry indices queue");
      pthread_mutex_lock (&ctx->is_writing_mutex);
      ctx->is_writing = false;
      pthread_mutex_unlock (&ctx->is_writing_mutex);
      return (S_FAILURE);
    }

  trace ("Got task from global queue");

  pthread_mutex_lock (&ctx->registry_used_mutex);
  ctx->registry_used[id] = true;
  pthread_mutex_unlock (&ctx->registry_used_mutex);
  trace ("Set id %d in registry_used as true", id);

  task_write_state_setup (ctx, id);

  return (push_job (ctx, send_task_job));
}

static status_t
send_config_job (void *arg)
{
  client_context_t *ctx = arg;

  status_t status;
  if (ctx->write_state.base_state.vec_sz != 0)
    {
      status = write_state_write (ctx->socket_fd, &ctx->write_state);
      if (status != S_SUCCESS)
        {
          error ("Could not send hash to client");
          return (S_FAILURE);
        }

      if (ctx->write_state.base_state.vec_sz != 0)
        return (push_job (ctx, send_config_job));
    }

  status = write_state_write_extra (ctx->socket_fd, &ctx->write_state);
  if (status != S_SUCCESS)
    {
      error ("Could not send alphabet to client");
      return (S_FAILURE);
    }

  return (push_job (ctx, ctx->write_state.vec_extra_sz != 0
                             ? send_config_job
                             : create_task_job));
}

static void
handle_read (evutil_socket_t socket_fd, short what, void *arg)
{
  assert (what == EV_READ);
  /* We already have socket_fd in client_context_t */
  (void)socket_fd; /* to suppress "unused parameter" warning */

  client_context_t *ctx = arg;
  mt_context_t *mt_ctx = &ctx->rsrv_ctx->srv_base.mt_ctx;

  size_t bytes_read
      = readv (ctx->socket_fd, ctx->read_state.vec, ctx->read_state.vec_sz);
  if ((ssize_t)bytes_read <= 0)
    {
      error ("Could not read result from a client");
      client_context_destroy (ctx);
      return;
    }

  ctx->read_state.vec[0].iov_len -= bytes_read;
  ctx->read_state.vec[0].iov_base += bytes_read;

  if (ctx->read_state.vec[0].iov_len > 0)
    return;

  ctx->read_state.vec[0].iov_len = sizeof (ctx->read_buffer);
  ctx->read_state.vec[0].iov_base = &ctx->read_buffer;
  ctx->read_state.vec_sz = 1;

  result_t *result = &ctx->read_buffer;

  pthread_mutex_lock (&ctx->registry_used_mutex);
  bool is_used = ctx->registry_used[result->id];
  ctx->registry_used[result->id] = false;
  pthread_mutex_unlock (&ctx->registry_used_mutex);

  if (!is_used)
    {
      warn ("Unexpected result id: %d", result->id);
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
      if (srv_trysignal (mt_ctx) == S_FAILURE)
        return;
      event_base_loopbreak (ctx->rsrv_ctx->rctr_ctx.ev_base);
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
      client_context_t *client = NULL;
      if (queue_pop (&ctx->starving_clients, &client) != QS_SUCCESS)
        {
          error ("Could not pop from a starving clients queue");
          return (NULL);
        }
      trace ("Got starving client from queue");

      pthread_mutex_lock (&client->is_writing_mutex);
      if (client->is_writing)
        {
          pthread_mutex_unlock (&client->is_writing_mutex);
          continue;
        }
      client->is_writing = true;
      pthread_mutex_unlock (&client->is_writing_mutex);

      int id;
      if (queue_pop (&client->registry_idx, &id) != QS_SUCCESS)
        {
          error ("Could not pop index from registry indices queue");
          return (NULL);
        }
      trace ("Got id for a starving client: %d", id);

      task_t *task = &client->registry[id];
      if (queue_pop (&ctx->srv_base.mt_ctx.queue, task) != QS_SUCCESS)
        {
          error ("Could not pop from the global queue");
          return (NULL);
        }
      trace ("Got task for a starving client");

      task_write_state_setup (client, id);

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

static void
handle_accept_error (struct evconnlistener *listener, void *arg)
{
  (void)listener;

  warn ("Got error on connection accept: %m");
  rsrv_context_t *ctx = arg;
  event_base_loopbreak (ctx->rctr_ctx.ev_base);
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

  client_context_t *client_ctx = client_context_init (srv_ctx, fd);
  if (!client_ctx)
    goto fail;

  trace ("Got new connection and created new event");

  if (event_add (client_ctx->read_event, NULL) != 0)
    {
      error ("Could not add event to event base");
      goto destroy_ctx;
    }

  trace ("Added new event");

  if (push_job (client_ctx, send_config_job) == S_FAILURE)
    {
      error ("Could not add send_config job");
      goto destroy_ctx;
    }

  return;

destroy_ctx:
  client_context_destroy (client_ctx);
  client_ctx = NULL;
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
  reactor_context_t *rctr_ctx_ptr = &rsrv_ctx.rctr_ctx;

  if (srv_base_context_init (&rsrv_ctx.srv_base, config) == S_FAILURE)
    {
      error ("Could not initialize server context");
      srv_base_context_destroy (&rsrv_ctx.srv_base);
      return (false);
    }

  if (rsrv_ctx_init (context_ptr) == S_FAILURE)
    goto fail;

  struct evconnlistener *listener = evconnlistener_new (
      rsrv_ctx.rctr_ctx.ev_base, handle_accept, &rsrv_ctx, LEV_OPT_REUSEABLE,
      -1, rsrv_ctx.srv_base.listen_fd);
  if (!listener)
    {
      error ("Could not create a listener");
      goto fail;
    }
  evconnlistener_set_error_cb (listener, handle_accept_error);

  thread_pool_t *thread_pool = &rsrv_ctx.srv_base.mt_ctx.thread_pool;

  int number_of_threads
      = (config->number_of_threads > 2) ? config->number_of_threads - 2 : 1;
  if (create_threads (thread_pool, number_of_threads, handle_io, &rctr_ctx_ptr,
                      sizeof (rctr_ctx_ptr), "i/o handler")
      == 0)
    goto free_listener;
  trace ("Created I/O handler thread");

  if (!thread_create (thread_pool, handle_starving_clients, &context_ptr,
                      sizeof (context_ptr), "starving clients handler"))
    goto free_listener;
  trace ("Created starving clients handler thread");

  if (!thread_create (thread_pool, dispatch_event_loop, &rsrv_ctx.rctr_ctx,
                      sizeof (rsrv_ctx.rctr_ctx), "event loop dispatcher"))
    goto free_listener;
  trace ("Created event loop dispatcher thread");

  mt_context_t *mt_ctx = (mt_context_t *)&rsrv_ctx;
  if (process_tasks (task, config, mt_ctx) == S_FAILURE)
    goto free_listener;

  evconnlistener_free (listener);

  if (rsrv_context_destroy (&rsrv_ctx) == S_FAILURE)
    error ("Could not destroy server context");

  return (mt_ctx->password[0] != 0);

free_listener:
  if (listener)
    evconnlistener_free (listener);
fail:
  trace ("Failed, destroying server context");
  if (rsrv_context_destroy (&rsrv_ctx) == S_FAILURE)
    error ("Could not destroy server context");

  return (false);
}
