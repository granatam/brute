#include "sync_client.h"

#include "brute.h"
#include "client_common.h"
#include "common.h"
#include "log.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <sys/socket.h>
#include <sys/types.h>
#endif

void
sync_client_find_password (task_t *task, config_t *config, st_context_t *ctx)
{
  task->result.is_correct = brute (task, config, st_password_check, ctx);

  if (!task->result.is_correct)
    memset (task->result.password, 0, sizeof (task->result.password));
}

static status_t
handle_task (client_base_context_t *client_base, task_t *task, void *arg)
{
  st_context_t *st_ctx = arg;

  st_ctx->hash = client_base->hash;

  if (client_base->config->timeout > 0
      && ms_sleep (client_base->config->timeout) != 0)
    error ("Could not sleep");

  if (client_base->task_cb != NULL)
    client_base->task_cb (task, client_base->config, st_ctx);

  result_t task_result = task->result;
  struct iovec vec[]
      = { { .iov_base = &task_result, .iov_len = sizeof (task_result) } };
  if (send_wrapper (client_base->socket_fd, vec,
                    sizeof (vec) / sizeof (vec[0]))
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
run_client (config_t *config, task_callback_t task_cb)
{
  client_base_context_t client_base;
  if (client_base_context_init (&client_base, config, task_cb) == S_FAILURE)
    {
      error ("Could not initialize client base context");
      return (false);
    }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr (config->addr);
  addr.sin_port = htons (config->port);

  if (connect (client_base.socket_fd, (struct sockaddr *)&addr, sizeof (addr))
      == -1)
    {
      error ("Could not connect to server");
      return (false);
    }

  task_t task;
  st_context_t st_context = {
    .data = { .initialized = 0 },
  };

  client_base_recv_loop (&client_base, &task, handle_task, &st_context);
  client_base_context_destroy (&client_base);

  return (false);
}

static void *
client_thread_helper (void *arg)
{
  client_base_context_t *ctx = *(client_base_context_t **)arg;
  run_client (ctx->config, ctx->task_cb);

  return (NULL);
}

void
spawn_clients (config_t *config, task_callback_t task_cb)
{
  thread_pool_t thread_pool;
  if (thread_pool_init (&thread_pool) == S_FAILURE)
    {
      error ("Could not initialize a thread pool");
      return;
    }

  client_base_context_t context = {
    .config = config,
    .task_cb = task_cb,
  };
  client_base_context_t *context_ptr = &context;

  if (create_threads (&thread_pool, config->number_of_threads,
                      &client_thread_helper, &context_ptr,
                      sizeof (context_ptr), "sync client")
      == 0)
    return;

  if (thread_pool_join (&thread_pool) == S_FAILURE)
    error ("Could not wait for all threads to end");

  trace ("Waited for all threads to end");
}
