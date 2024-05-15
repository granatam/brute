#include "async_client.h"

#include "brute.h"
#include "client_common.h"
#include "common.h"
#include "single.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void *
client_worker (void *arg)
{
  async_client_context_t *ctx = *(async_client_context_t **)arg;

  st_context_t st_context = {
    .hash = ctx->config->hash,
    .data = { .initialized = 0 },
  };

  while (true)
    {
      task_t task;
      if (queue_pop (&ctx->task_queue, &task) != QS_SUCCESS)
        return (NULL);
      print_error ("[worker] After task_queue pop\n");
      task.task.is_correct
          = brute (&task, ctx->config, st_password_check, &st_context);

      if (queue_push (&ctx->result_queue, &task.task) != QS_SUCCESS)
        return (NULL);
      print_error ("[worker] After result_queue push\n");
    }
  return (NULL);
}

static void *
task_receiver (void *arg)
{
  async_client_context_t *ctx = *(async_client_context_t **)arg;

  task_t task;
  while (true)
    {
      command_t cmd;
      if (recv_wrapper (ctx->socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
        {
          print_error ("Could not receive command from server\n");
          goto end;
        }

      switch (cmd)
        {
        case CMD_ALPH:
          if (handle_alph (ctx->socket_fd, ctx->config, ctx->alph)
              == S_FAILURE)
            {
              print_error ("Could not handle alphabet\n");
              goto end;
            }
          print_error ("[receiver] Received alph\n");
          break;
        case CMD_HASH:
          if (handle_hash (ctx->socket_fd, ctx->hash) == S_FAILURE)
            {
              print_error ("Could not handle hash\n");
              goto end;
            }
          print_error ("[acl receiver] Received hash %s at %p, "
                       "ctx->config->hash is %p\n",
                       ctx->hash, ctx->hash, ctx->config->hash);
          break;
        case CMD_EXIT:
          print_error ("[acl receiver] Received exit\n");
          goto end;
        case CMD_TASK:
          if (recv_wrapper (ctx->socket_fd, &task, sizeof (task_t), 0)
              == S_FAILURE)
            {
              print_error ("Could not receive task from server\n");
              goto end;
            }
          print_error ("[acl receiver] Received task\n");

          if (queue_push (&ctx->task_queue, &task) != QS_SUCCESS)
            goto end;
          print_error ("[acl receiver] Pushed task to queue\n");

          break;
        }
    }

end:
  print_error ("[acl receiver] end mark\n");
  ctx->done = true;
  if (pthread_cond_signal (&ctx->cond_sem) != 0)
    {
      print_error ("Could not signal on a conditional semaphore\n");
    }

  print_error ("[acl receiver] after signal\n");

  return (NULL);
}

static void *
result_sender (void *arg)
{
  async_client_context_t *ctx = *(async_client_context_t **)arg;

  result_t result;
  while (true)
    {
      if (queue_pop (&ctx->result_queue, &result) != QS_SUCCESS)
        return (NULL);
      print_error ("[acl sender] After result_queue pop\n");

      if (send_wrapper (ctx->socket_fd, &result, sizeof (result), 0)
          == S_FAILURE)
        {
          print_error ("Could not send result to server\n");
          return (NULL);
        }
      print_error ("[acl sender] After task send\n");
    }

  return (NULL);
}

bool
run_async_client (config_t *config)
{
  async_client_context_t ctx;
  memset (&ctx, 0, sizeof (ctx));

  if (thread_pool_init (&ctx.thread_pool) == S_FAILURE)
    {
      print_error ("Could not initialize thread pool\n");
      return (false);
    }
  if (queue_init (&ctx.task_queue, sizeof (task_t)) != QS_SUCCESS)
    {
      print_error ("Could not initialize task queue\n");
      return (false);
    }
  if (queue_init (&ctx.result_queue, sizeof (result_t)) != QS_SUCCESS)
    {
      print_error ("Could not initialize result queue\n");
      return (false);
    }
  if (pthread_mutex_init (&ctx.mutex, NULL) != 0)
    {
      print_error ("Could not initialize mutex\n");
      return (false);
    }
  if (pthread_cond_init (&ctx.cond_sem, NULL) != 0)
    {
      print_error ("Could not initialize conditional semaphore\n");
      return (false);
    }
  ctx.config = config;
  ctx.done = false;
  ctx.config->hash = ctx.hash;
  ctx.config->alph = ctx.alph;

  ctx.socket_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (ctx.socket_fd == -1)
    {
      print_error ("Could not initialize client socket\n");
      return (false);
    }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr (config->addr);
  addr.sin_port = htons (config->port);

  if (connect (ctx.socket_fd, (struct sockaddr *)&addr, sizeof (addr)) == -1)
    {
      print_error ("Could not connect to server\n");
      return (false);
    }

  async_client_context_t *ctx_ptr = &ctx;

  thread_create (&ctx.thread_pool, task_receiver, &ctx_ptr, sizeof (ctx_ptr));
  print_error ("[async client] Created receiver thread\n");
  thread_create (&ctx.thread_pool, result_sender, &ctx_ptr, sizeof (ctx_ptr));
  print_error ("[async client] Created sender thread\n");
  create_threads (&ctx.thread_pool, config->number_of_threads, client_worker,
                  &ctx_ptr, sizeof (ctx_ptr));
  print_error ("[async client] Created worker threads\n");

  if (pthread_mutex_lock (&ctx.mutex) != 0)
    {
      print_error ("Could not lock a mutex\n");
      return (S_FAILURE);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &ctx.mutex);

  while (!ctx.done)
    if (pthread_cond_wait (&ctx.cond_sem, &ctx.mutex) != 0)
      {
        print_error ("Could not wait on a condition\n");
        return (S_FAILURE);
      }

  pthread_cleanup_pop (!0);

  print_error ("[async client] After wait\n");

  if (queue_cancel (&ctx.task_queue) != QS_SUCCESS)
    {
      print_error ("Could not cancel task queue\n");
      return (false);
    }
  if (queue_cancel (&ctx.result_queue) != QS_SUCCESS)
    {
      print_error ("Could not cancel result queue\n");
      return (false);
    }
  if (thread_pool_cancel (&ctx.thread_pool) == S_FAILURE)
    {
      print_error ("Could not cancel thread pool\n");
      return (false);
    }

  print_error ("[async client] After thread pool cancel\n");

  shutdown (ctx.socket_fd, SHUT_RDWR);
  if (close (ctx.socket_fd) != 0)
    print_error ("Could not close socket\n");

  return (false);
}
