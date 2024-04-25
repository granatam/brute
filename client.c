#include "client.h"

#include "brute.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static status_t
handle_alph (int socket_fd, config_t *config, char *alph)
{
  int32_t length;
  if (recv_wrapper (socket_fd, &length, sizeof (length), 0) == S_FAILURE)
    {
      print_error ("Could not receive alphabet length from server\n");
      return (S_FAILURE);
    }

  if (recv_wrapper (socket_fd, alph, length, 0) == S_FAILURE)
    {
      print_error ("Could not receive alphabet from server\n");
      return (S_FAILURE);
    }

  config->alph = alph;

  return (S_SUCCESS);
}

static status_t
handle_hash (int socket_fd, char *hash, st_context_t *ctx)
{
  if (recv_wrapper (socket_fd, hash, HASH_LENGTH, 0) == S_FAILURE)
    {
      print_error ("Could not receive hash from server\n");
      return (S_FAILURE);
    }
  hash[HASH_LENGTH - 1] = 0;

  ctx->hash = hash;

  return (S_SUCCESS);
}

// TODO: Should it return status_t now?
status_t
find_password (int socket_fd, task_t *task, config_t *config,
               st_context_t *ctx)
{
  if (brute (task, config, st_password_check, ctx))
    task->task.is_correct = true;
  else
    memset (task->task.password, 0, sizeof (task->task.password));

  return (S_SUCCESS);
}

static status_t
handle_task (int socket_fd, task_t *task, config_t *config, st_context_t *ctx,
             task_callback_t task_callback)
{
  if (recv_wrapper (socket_fd, task, sizeof (task_t), 0) == S_FAILURE)
    {
      print_error ("Could not receive task from server\n");
      return (S_FAILURE);
    }
  print_error("Received task\n");

  if (task_callback != NULL)
    {
      if (task_callback (socket_fd, task, config, ctx) == S_FAILURE)
        return (S_FAILURE);
    }

  result_t task_result = task->task;
  if (send_wrapper (socket_fd, &task_result, sizeof (task_result), 0)
      == S_FAILURE)
    {
      print_error ("Could not send result to server\n");
      return (S_FAILURE);
    }
  print_error("Sent result\n");

  return (S_SUCCESS);
}

bool
run_client (task_t *task, config_t *config, task_callback_t task_callback)
{
  int socket_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1)
    {
      print_error ("Could not initialize client socket\n");
      return (false);
    }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr (config->addr);
  addr.sin_port = htons (config->port);

  if (connect (socket_fd, (struct sockaddr *)&addr, sizeof (addr)) == -1)
    {
      print_error ("Could not connect to server\n");
      return (false);
    }

  char hash[HASH_LENGTH];
  char alph[MAX_ALPH_LENGTH];

  st_context_t st_context = {
    .data = { .initialized = 0 },
  };

  while (true)
    {
      command_t cmd;
      if (recv_wrapper (socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
        {
          print_error ("Could not receive command from server\n");
          goto end;
        }

      switch (cmd)
        {
        case CMD_ALPH:
          if (handle_alph (socket_fd, config, alph) == S_FAILURE)
            {
              print_error ("Could not handle alphabet\n");
              goto end;
            }
          break;
        case CMD_HASH:
          if (handle_hash (socket_fd, hash, &st_context) == S_FAILURE)
            {
              print_error ("Could not handle hash\n");
              goto end;
            }
          break;
        case CMD_EXIT:
          goto end;
        case CMD_TASK:
          if (handle_task (socket_fd, task, config, &st_context, task_callback)
              == S_FAILURE)
            goto end;
          break;
        }
    }

end:
  shutdown (socket_fd, SHUT_RDWR);
  close (socket_fd);
  return (false);
}

static void *
client_thread_helper (void *arg)
{
  client_context_t *ctx = *(client_context_t **)arg;

  task_t task;
  ctx->task = &task;

  run_client (ctx->task, ctx->config, ctx->task_callback);

  return (NULL);
}

void
spawn_clients (task_t *task, config_t *config, task_callback_t task_callback)
{
  thread_pool_t thread_pool;
  if (thread_pool_init (&thread_pool) == S_FAILURE)
    {
      print_error ("Could not initialize a thread pool\n");
      return;
    }

  client_context_t context = {
    .task = task,
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
    print_error ("Could not wait for a thread pool to end\n");
}
