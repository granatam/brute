#include "client.h"

#include "brute.h"
#include "common.h"
#include "single.h"

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
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons (9000);

  if (connect (socket_fd, (struct sockaddr *)&addr, sizeof (addr)) == -1)
    {
      print_error ("Could not connect to server\n");
      return (false);
    }

  st_context_t st_context = {
    .hash = config->hash,
    .data = { .initialized = 0 },
  };

  while (true)
    {
      if (recv (socket_fd, task, sizeof (task_t), 0) == -1)
        {
          print_error ("Could not receive data from server\n");
          goto fail;
        }

      if (brute (task, config, st_password_check, &st_context))
        {
          int password_size = sizeof (task->password);
          if (send (socket_fd, &password_size, sizeof (int), 0) == -1)
            {
              print_error ("Could not send data to server\n");
              goto fail;
            }

          if (send (socket_fd, task->password, password_size, 0) == -1)
            {
              print_error ("Could not send data to server\n");
              goto fail;
            }

          return (true);
        }

      int wrong_password = 0;
      if (send (socket_fd, &wrong_password, sizeof (int), 0) == -1)
        {
          print_error ("Could not send data to server\n");
          goto fail;
        }
    }

fail:
  close (socket_fd);
  return (false);
}
