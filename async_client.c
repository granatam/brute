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
      queue_pop (&ctx->task_queue, &task);
      print_error("[worker] After task_queue pop\n");
      bool is_found
          = brute (&task, ctx->config, st_password_check, &st_context);
      task.task.is_correct = is_found;

      queue_push (&ctx->result_queue, &task.task);
      print_error("[worker] After result_queue push\n");
    }
  return (NULL);
}

static void *
task_receiver (void *arg)
{
  async_client_context_t *ctx = *(async_client_context_t **)arg;

  char hash[HASH_LENGTH];
  char alph[MAX_ALPH_LENGTH];

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
          if (handle_alph (ctx->socket_fd, ctx->config, alph) == S_FAILURE)
            {
              print_error ("Could not handle alphabet\n");
              goto end;
            }
            print_error("[receiver] Received alph\n");
          break;
        case CMD_HASH:
          if (handle_hash (ctx->socket_fd, hash) == S_FAILURE)
            {
              print_error ("Could not handle hash\n");
              goto end;
            }
          print_error("[receiver] Received hash %s at %p, ctx->config->hash is %p\n", hash, hash, ctx->config->hash);
          ctx->config->hash = hash;
          break;
        case CMD_EXIT:
          print_error("[receiver] Received exit\n");
          goto end;
        case CMD_TASK:
          if (recv_wrapper (ctx->socket_fd, &task, sizeof (task_t), 0)
              == S_FAILURE)
            {
              print_error ("Could not receive task from server\n");
              goto end;
            }
          print_error("[receiver] Received task\n");
          queue_push (&ctx->task_queue, &task);
          print_error("[receiver] Pushed task to queue\n");
          break;
        }
    }

end:
  shutdown (ctx->socket_fd, SHUT_RDWR);
  close (ctx->socket_fd);
  return (NULL);
}

static void *
result_sender (void *arg)
{
  async_client_context_t *ctx = *(async_client_context_t **)arg;

  result_t task;
  while (true)
    {
      queue_pop (&ctx->result_queue, &task);
      print_error ("[sender] After result_queue pop\n");
      send_wrapper (ctx->socket_fd, &task, sizeof (task), 0);
      print_error ("[sender] After task send\n");
    }

  return (NULL);
}

bool
run_async_client (task_t *task, config_t *config)
{
  async_client_context_t ctx;

  thread_pool_init (&ctx.thread_pool);
  queue_init (&ctx.task_queue, sizeof (task_t));
  queue_init (&ctx.result_queue, sizeof (result_t));
  pthread_mutex_init (&ctx.mutex, NULL);
  ctx.config = config;

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
  print_error("[main] Created receiver thread\n");
  thread_create (&ctx.thread_pool, result_sender, &ctx_ptr, sizeof (ctx_ptr));
  print_error("[main] Created sender thread\n");
  create_threads (&ctx.thread_pool, config->number_of_threads - 2,
                  client_worker, &ctx_ptr, sizeof (ctx_ptr));
  print_error("[main] Created worker threads\n");

end:
  shutdown (ctx.socket_fd, SHUT_RDWR);
  close (ctx.socket_fd);
  return (false);
}
