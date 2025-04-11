#include "client_common.h"

#include "log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

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
client_base_context_destroy (client_base_context_t *client_base)
{
  shutdown (client_base->socket_fd, SHUT_RDWR);
  close (client_base->socket_fd);
  trace ("Closed connection with server");

  return (S_SUCCESS);
}

status_t
client_base_recv_loop (client_base_context_t *client_base, task_t *task,
                       client_task_handler_t task_hdlr, void *arg)
{
  status_t status = S_SUCCESS;
  while (true)
    {
      command_t cmd;
      if (recv_wrapper (client_base->socket_fd, &cmd, sizeof (cmd), 0)
          == S_FAILURE)
        {
          error ("Could not receive command from server");
          status = S_FAILURE;
          goto end;
        }

      switch (cmd)
        {
        case CMD_ALPH:
          if (handle_alph (client_base->socket_fd, client_base->alph)
              == S_FAILURE)
            {
              error ("Could not handle alphabet");
              status = S_FAILURE;
              goto end;
            }
          trace ("Received alphabet '%s' from server", client_base->alph);
          break;
        case CMD_HASH:
          if (handle_hash (client_base->socket_fd, client_base->hash)
              == S_FAILURE)
            {
              error ("Could not handle hash");
              status = S_FAILURE;
              goto end;
            }
          trace ("Received hash '%s' from server", client_base->hash);
          break;
        case CMD_TASK:
          if (recv_wrapper (client_base->socket_fd, task, sizeof (task_t), 0)
              == S_FAILURE)
            {
              error ("Could not receive task from server");
              status = S_FAILURE;
              goto end;
            }
          trace ("Received task from server");
          task_hdlr (client_base, task, arg);
          break;
        }
    }

end:
  trace ("Receiving information from server is finished");
  return (status);
}

status_t
handle_alph (int socket_fd, char *alph)
{
  int32_t length;
  if (recv_wrapper (socket_fd, &length, sizeof (length), 0) == S_FAILURE)
    {
      error ("Could not receive alphabet length from server");
      return (S_FAILURE);
    }

  if (recv_wrapper (socket_fd, alph, length, 0) == S_FAILURE)
    {
      error ("Could not receive alphabet from server");
      return (S_FAILURE);
    }
  alph[length] = 0;

  return (S_SUCCESS);
}

status_t
handle_hash (int socket_fd, char *hash)
{
  if (recv_wrapper (socket_fd, hash, HASH_LENGTH, 0) == S_FAILURE)
    {
      error ("Could not receive hash from server");
      return (S_FAILURE);
    }
  hash[HASH_LENGTH - 1] = 0;

  return (S_SUCCESS);
}

int
ms_sleep (long milliseconds)
{
  struct timespec duration, rem;
  if (milliseconds >= 1000)
    {
      duration.tv_sec = milliseconds / 1000;
      duration.tv_nsec = (milliseconds % 1000) * 1000000;
    }
  else
    {
      duration.tv_sec = 0;
      duration.tv_nsec = milliseconds * 1000000;
    }
  return nanosleep (&duration, &rem);
}
