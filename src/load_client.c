#include "load_client.h"
#include "log.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <event2/event.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_LOAD_CLIENTS 10

// TODO: handle_read

void *
spawn_client (void *cfg)
{
  // TODO: Allocate a client's context on heap and connect to the server with
  // an exponential retry. Add read event to save the context.
}

status_t
spawn_load_clients (config_t *config)
{
  thread_pool_t thread_pool;
  if (thread_pool_init (&thread_pool) == S_FAILURE)
    {
      error ("Could not initialize a thread pool");
      return (S_FAILURE);
    }

  if (!thread_create (&thread_pool, spawn_client, config, sizeof (config),
                      "client spawner"))
    {
      error ("Could not create a spawner thread");
      goto fail;
    }

  // TODO: Spawn N I/O threads.

  if (thread_pool_join (&thread_pool) == S_FAILURE)
    error ("Could not wait for all threads to end");

fail:
  // TODO: Destroy the thread pool.
  return (S_FAILURE);
}
