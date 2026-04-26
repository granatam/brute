#include "server_common.h"

#include "common.h"
#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAX_CONN_QUEUE_LEN (10)

status_t
srv_listener_init (srv_listener_t *listener, config_t *config)
{
  listener->listen_fd = -1;

  listener->listen_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (listener->listen_fd == -1)
    {
      error ("Could not initialize server socket");
      return S_FAILURE;
    }

  int option = 1;
  if (setsockopt (listener->listen_fd, SOL_SOCKET, SO_REUSEADDR, &option,
                  sizeof (option))
      == -1)
    {
      error ("Could not set socket option");
      goto fail;
    }

  struct sockaddr_in srv_addr;
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_addr.s_addr = inet_addr (config->addr);
  srv_addr.sin_port = htons (config->port);

  if (bind (listener->listen_fd, (struct sockaddr *)&srv_addr,
            sizeof (srv_addr))
      == -1)
    {
      error ("Could not bind socket");
      goto fail;
    }

  if (listen (listener->listen_fd, MAX_CONN_QUEUE_LEN) == -1)
    {
      error ("Could not start listening to incoming connections");
      goto fail;
    }

  return S_SUCCESS;

fail:
  close (listener->listen_fd);
  listener->listen_fd = -1;
  return S_FAILURE;
}

status_t
srv_listener_destroy (srv_listener_t *listener)
{
  if (listener->listen_fd >= 0)
    {
      shutdown (listener->listen_fd, SHUT_RDWR);

      if (close (listener->listen_fd) != 0)
        {
          error ("Could not close server socket");
          return S_FAILURE;
        }

      listener->listen_fd = -1;
    }

  return S_SUCCESS;
}

status_t
accept_client (int srv_socket_fd, int *client_socket_fd)
{
  while (true)
    {
      if (srv_socket_fd < 0)
        {
          error ("Invalid server socket");
          return S_FAILURE;
        }

      *client_socket_fd = accept (srv_socket_fd, NULL, NULL);
      if (*client_socket_fd == -1)
        {
          error ("Could not accept new connection: %s", strerror (errno));

          if (errno == EINVAL || errno == EBADF)
            return S_FAILURE;

          continue;
        }

      break;
    }

  trace ("Accepted new connection");

  int option = 1;
  if (setsockopt (*client_socket_fd, IPPROTO_TCP, TCP_NODELAY, &option,
                  sizeof (option))
      == -1)
    {
      error ("Could not set socket option");
      close (*client_socket_fd);
      *client_socket_fd = -1;
      return S_FAILURE;
    }

  return S_SUCCESS;
}

status_t
send_hash (int socket_fd, config_t *config)
{
  command_t cmd = CMD_HASH;

  struct iovec vec[] = {
    { .iov_base = &cmd, .iov_len = sizeof (cmd) },
    { .iov_base = config->hash, .iov_len = HASH_LENGTH },
  };

  if (send_wrapper (socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
      == S_FAILURE)
    {
      error ("Could not send hash to client");
      return S_FAILURE;
    }

  return S_SUCCESS;
}

status_t
send_alph (int socket_fd, config_t *config)
{
  command_t cmd = CMD_ALPH;
  uint32_t length = strlen (config->alph);

  struct iovec vec[] = {
    { .iov_base = &cmd, .iov_len = sizeof (cmd) },
    { .iov_base = &length, .iov_len = sizeof (length) },
    { .iov_base = config->alph, .iov_len = length },
  };

  if (send_wrapper (socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
      == S_FAILURE)
    {
      error ("Could not send alphabet to client");
      return S_FAILURE;
    }

  return S_SUCCESS;
}

status_t
send_config_data (int socket_fd, config_t *config)
{
  if (send_hash (socket_fd, config) == S_FAILURE)
    return S_FAILURE;

  trace ("Sent hash to client");

  if (send_alph (socket_fd, config) == S_FAILURE)
    return S_FAILURE;

  trace ("Sent alphabet to client");

  return S_SUCCESS;
}

status_t
send_task (int socket_fd, task_t *task)
{
  command_t cmd = CMD_TASK;
  task_t wire_task = *task;

  wire_task.result.is_correct = false;

  struct iovec vec[] = {
    { .iov_base = &cmd, .iov_len = sizeof (cmd) },
    { .iov_base = &wire_task, .iov_len = sizeof (wire_task) },
  };

  if (send_wrapper (socket_fd, vec, sizeof (vec) / sizeof (vec[0]))
      == S_FAILURE)
    {
      error ("Could not send task to client");
      return S_FAILURE;
    }

  trace ("Sent task %s to client", wire_task.result.password);

  return S_SUCCESS;
}
