#include "client.h"

#include "brute.h"
#include "common.h"
#include "single.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

bool
run_client (task_t *task, config_t *config)
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

  int hash_size;
  if (recv_wrapper (socket_fd, &hash_size, sizeof (int), 0) == S_FAILURE)
    {
      print_error ("Could not receive hash size from server\n");
      close (socket_fd);
      return (false);
    }

  char hash[hash_size];
  if (recv_wrapper (socket_fd, hash, hash_size, 0) == S_FAILURE)
    {
      print_error ("Could not receive hash from server\n");
      goto fail;
    }

  st_context_t st_context = {
    .hash = hash,
    .data = { .initialized = 0 },
  };

  while (true)
    {
      if (recv_wrapper (socket_fd, task, sizeof (task_t), 0) == S_FAILURE)
        {
          print_error ("Could not receive data from server\n");
          goto fail;
        }

      if (brute (task, config, st_password_check, &st_context))
        {
          int password_size = sizeof (task->password);
          if (send_wrapper (socket_fd, &password_size, sizeof (int), 0)
              == S_FAILURE)
            {
              print_error ("Could not send data to server\n");
              goto fail;
            }

          if (send_wrapper (socket_fd, task->password, password_size, 0)
              == S_FAILURE)
            {
              print_error ("Could not send data to server\n");
              goto fail;
            }
          return (true);
        }

      int wrong_password = 0;
      if (send_wrapper (socket_fd, &wrong_password, sizeof (int), 0)
          == S_FAILURE)
        {
          print_error ("Could not send data to server\n");
          goto fail;
        }
    }

fail:
  close (socket_fd);
  return (false);
}
