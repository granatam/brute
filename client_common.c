#include "client_common.h"

#include "brute.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

status_t
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

status_t
handle_hash (int socket_fd, char *hash)
{
  if (recv_wrapper (socket_fd, hash, HASH_LENGTH, 0) == S_FAILURE)
    {
      print_error ("Could not receive hash from server\n");
      return (S_FAILURE);
    }
  hash[HASH_LENGTH - 1] = 0;

  return (S_SUCCESS);
}
