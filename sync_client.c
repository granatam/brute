#include "sync_client.h"

#include "brute.h"
#include "client_common.h"
#include "log.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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
      error ("Could not receive task from server");
      return (S_FAILURE);
    }
  trace ("Received task from server");

  if (config->timeout > 0)
    if (ms_sleep (config->timeout) != 0)
      error ("Could not sleep");

  if (task_callback != NULL)
    if (task_callback (task, config, ctx) == S_FAILURE)
      return (S_FAILURE);

  result_t task_result = task->task;
  struct iovec vec[]
      = { { .iov_base = &task_result, .iov_len = sizeof (task_result) } };
  if (send_wrapper (socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
      == S_FAILURE)
    {
      error ("Could not send result to server");
      return (S_FAILURE);
    }

  trace ("Sent %s result %s to server",
         task_result.is_correct ? "correct" : "incorrect",
         task_result.password);

  return (S_SUCCESS);
}

bool
run_client (config_t *config, task_callback_t task_callback)
{
  int socket_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1)
    {
      error ("Could not initialize client socket");
      return (false);
    }

  int option = 1;
  setsockopt (socket_fd, SOL_SOCKET, SO_KEEPALIVE, &option, sizeof (option));

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr (config->addr);
  addr.sin_port = htons (config->port);

  if (connect (socket_fd, (struct sockaddr *)&addr, sizeof (addr)) == -1)
    {
      error ("Could not connect to server");
      return (false);
    }

  setsockopt (socket_fd, SOL_SOCKET, TCP_NODELAY, &option, sizeof (option));

  char hash[HASH_LENGTH];
  char alph[MAX_ALPH_LENGTH];
  task_t task;

  st_context_t st_context = {
    .data = { .initialized = 0 },
  };

  while (true)
    {
      trace ("Waiting for a command");

      command_t cmd;
      if (recv_wrapper (socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
        {
          error ("Could not receive command from server");
          goto end;
        }

      trace ("Received command from server");

      switch (cmd)
        {
        case CMD_ALPH:
          if (handle_alph (socket_fd, alph) == S_FAILURE)
            {
              error ("Could not handle alphabet");
              goto end;
            }
          trace ("Received alphabet from server");
          break;
        case CMD_HASH:
          if (handle_hash (socket_fd, hash) == S_FAILURE)
            {
              error ("Could not handle hash");
              goto end;
            }
          st_context.hash = hash;
          trace ("Received hash from server");
          break;
        case CMD_TASK:
          trace ("Received task command from server");
          if (handle_task (socket_fd, &task, config, &st_context,
                           task_callback)
              == S_FAILURE)
            goto end;
          break;
        }
    }

end:
  trace ("Disconnected from server, not receiving anything from now");

  shutdown (socket_fd, SHUT_RDWR);
  close (socket_fd);
  trace ("Closed connection with server");

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
      error ("Could not initialize a thread pool");
      return;
    }

  client_context_t context = {
    .config = config,
    .task_callback = task_callback,
  };
  client_context_t *context_ptr = &context;

  if (create_threads (&thread_pool, config->number_of_threads,
                      &client_thread_helper, &context_ptr,
                      sizeof (context_ptr), "sync client")
      == 0)
    return;

  if (thread_pool_join (&thread_pool) == S_FAILURE)
    error ("Could not wait for all threads to end");

  trace ("Waited for all threads to end");
}
