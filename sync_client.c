#include "sync_client.h"

#include "brute.h"
#include "client_common.h"
#include "log.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// TODO: Should it return status_t now?
status_t
find_password (task_t *task, config_t *config, st_context_t *ctx)
{
  task->task.is_correct = brute (task, config, st_password_check, ctx);

  if (!task->task.is_correct)
    memset (task->task.password, 0, sizeof (task->task.password));

  return (S_SUCCESS);
}

static status_t
handle_task (int socket_fd, task_t *task, config_t *config, st_context_t *ctx,
             task_callback_t task_callback)
{
  if (recv_wrapper (socket_fd, task, sizeof (task_t), 0) == S_FAILURE)
    {
      error ("Could not receive task from server\n");
      return (S_FAILURE);
    }
  error ("[sync client] Received task\n");

  if (task_callback != NULL)
    if (task_callback (task, config, ctx) == S_FAILURE)
      return (S_FAILURE);

  result_t task_result = task->task;
  if (send_wrapper (socket_fd, &task_result, sizeof (task_result), 0)
      == S_FAILURE)
    {
      error ("Could not send result to server\n");
      return (S_FAILURE);
    }
  error ("[sync client] Sent result %s with is_correct %d\n",
         task_result.password, task_result.is_correct);

  return (S_SUCCESS);
}

bool
run_client (config_t *config, task_callback_t task_callback)
{
  int socket_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1)
    {
      error ("Could not initialize client socket\n");
      return (false);
    }

  int option = 1;
  setsockopt (socket_fd, SOL_SOCKET, SO_KEEPALIVE, &option,
              sizeof (option));

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr (config->addr);
  addr.sin_port = htons (config->port);

  if (connect (socket_fd, (struct sockaddr *)&addr, sizeof (addr)) == -1)
    {
      error ("Could not connect to server\n");
      return (false);
    }

  char hash[HASH_LENGTH];
  char alph[MAX_ALPH_LENGTH];
  task_t task;

  st_context_t st_context = {
    .data = { .initialized = 0 },
  };

  while (true)
    {
      command_t cmd;
      if (recv_wrapper (socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
        {
          error ("Could not receive command from server\n");
          goto end;
        }

      switch (cmd)
        {
        case CMD_ALPH:
          if (handle_alph (socket_fd, config, alph) == S_FAILURE)
            {
              error ("Could not handle alphabet\n");
              goto end;
            }
          break;
        case CMD_HASH:
          if (handle_hash (socket_fd, hash) == S_FAILURE)
            {
              error ("Could not handle hash\n");
              goto end;
            }
          st_context.hash = hash;
          break;
        case CMD_TASK:
          if (handle_task (socket_fd, &task, config, &st_context,
                           task_callback)
              == S_FAILURE)
            goto end;
          break;
        }
    }

end:
  error ("[sync client] end mark\n");

  shutdown (socket_fd, SHUT_RDWR);
  close (socket_fd);
  return (false);
}

static void *
client_thread_helper (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;
  run_client (ctx->config, ctx->task_callback);

  return (NULL);
}

void
spawn_clients (config_t *config, task_callback_t task_callback)
{
  thread_pool_t thread_pool;
  if (thread_pool_init (&thread_pool) == S_FAILURE)
    {
      error ("Could not initialize a thread pool\n");
      return;
    }

  client_context_t context = {
    .config = config,
    .task_callback = task_callback,
  };
  client_context_t *context_ptr = &context;

  if (create_threads (&thread_pool, config->number_of_threads,
                      &client_thread_helper, &context_ptr,
                      sizeof (context_ptr))
      == 0)
    return;

  if (thread_pool_join (&thread_pool) == S_FAILURE)
    error ("Could not wait for a thread pool to end\n");
}
