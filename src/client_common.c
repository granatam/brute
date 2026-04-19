#include "client_common.h"

#include "log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MS_IN_SEC (1000L)
#define NS_IN_MSEC (1000000L)

status_t
client_base_context_init (client_base_context_t *client_base, config_t *config,
                          task_callback_t task_cb)
{
  client_base->config = config;
  client_base->task_cb = task_cb;

  client_base->socket_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (client_base->socket_fd == -1)
    {
      error ("Could not initialize client socket");
      return (S_FAILURE);
    }

  int option = 1;
  if (setsockopt (client_base->socket_fd, SOL_SOCKET, SO_KEEPALIVE, &option,
                  sizeof (option))
      == -1)
    {
      error ("Could not set socket option");
      return (S_FAILURE);
    }

  if (setsockopt (client_base->socket_fd, IPPROTO_TCP, TCP_NODELAY, &option,
                  sizeof (option))
      == -1)
    {
      error ("Could not set socket option");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
srv_connect (client_base_context_t *client_base)
{
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr (client_base->config->addr);
  addr.sin_port = htons (client_base->config->port);

  if (connect (client_base->socket_fd, (struct sockaddr *)&addr, sizeof (addr))
      == -1)
    {
      error ("Could not connect to server");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
client_base_context_destroy (client_base_context_t *client_base)
{
  shutdown (client_base->socket_fd, SHUT_RDWR);
  trace ("Closed connection with server");

  if (close (client_base->socket_fd) != 0)
    error ("Could not close socket");

  return (S_SUCCESS);
}

io_status_t
handle_alph (int socket_fd, char *alph)
{
  int length;
  io_status_t recv_status
      = recv_wrapper (socket_fd, &length, sizeof (length), 0);
  if (recv_status != IOS_SUCCESS)
    {
      if (recv_status == IOS_FAILURE)
        error ("Could not receive alphabet length from server");
      return (recv_status);
    }

  if (length > MAX_ALPH_LENGTH)
    {
      error ("Received invalid alphabet length: %zu", length);
      return (IOS_FAILURE);
    }

  recv_status = recv_wrapper (socket_fd, alph, length, 0);
  if (recv_status != IOS_SUCCESS)
    {
      if (recv_status == IOS_FAILURE)
        error ("Could not receive alphabet from server");
      return (recv_status);
    }
  alph[length] = 0;

  return (IOS_SUCCESS);
}

io_status_t
handle_hash (int socket_fd, char *hash)
{
  io_status_t status = recv_wrapper (socket_fd, hash, HASH_LENGTH, 0);
  if (status != IOS_SUCCESS)
    {
      if (status == IOS_FAILURE)
        error ("Could not receive hash from server");
      return (status);
    }
  hash[HASH_LENGTH - 1] = 0;

  return (status);
}

void
client_base_recv_loop (client_base_context_t *client_base, task_t *task,
                       client_task_handler_t task_hdlr, void *arg)
{
  while (true)
    {
      command_t cmd;
      io_status_t recv_status
          = recv_wrapper (client_base->socket_fd, &cmd, sizeof (cmd), 0);
      if (recv_status != IOS_SUCCESS)
        {
          if (recv_status == IOS_FAILURE)
            error ("Could not receive command from server");
          break;
        }

      switch (cmd)
        {
        case CMD_ALPH:
          recv_status
              = handle_alph (client_base->socket_fd, client_base->alph);
          if (recv_status != IOS_SUCCESS)
            {
              if (recv_status == IOS_FAILURE)
                error ("Could not handle alphabet");
              goto end;
            }
          trace ("Received alphabet '%s' from server", client_base->alph);
          break;
        case CMD_HASH:
          recv_status
              = handle_hash (client_base->socket_fd, client_base->hash);
          if (recv_status != IOS_SUCCESS)
            {
              if (recv_status == IOS_FAILURE)
                error ("Could not handle hash");
              goto end;
            }
          trace ("Received hash '%s' from server", client_base->hash);
          break;
        case CMD_TASK:
          recv_status = recv_wrapper (client_base->socket_fd, task,
                                      sizeof (task_t), 0);
          if (recv_status != IOS_SUCCESS)
            {
              if (recv_status == IOS_FAILURE)
                error ("Could not receive task from server");
              goto end;
            }
          trace ("Received task from server");
          task_hdlr (client_base, task, arg);
          break;
        }
    }

end:
  trace ("Receiving information from server is finished");
}

int
ms_sleep (long milliseconds)
{
  struct timespec duration, rem;
  if (milliseconds >= MS_IN_SEC)
    {
      duration.tv_sec = milliseconds / MS_IN_SEC;
      duration.tv_nsec = (milliseconds % MS_IN_SEC) * NS_IN_MSEC;
    }
  else
    {
      duration.tv_sec = 0;
      duration.tv_nsec = milliseconds * NS_IN_MSEC;
    }
  return nanosleep (&duration, &rem);
}
