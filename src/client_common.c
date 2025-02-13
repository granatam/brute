#include "client_common.h"

#include "log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
  struct timespec time, time2;
  if (milliseconds >= 1000)
    {
      time.tv_sec = milliseconds / 1000;
      time.tv_nsec = (milliseconds % 1000) * 1000000;
    }
  else
    {
      time.tv_sec = 0;
      time.tv_nsec = milliseconds * 1000000;
    }
  return nanosleep (&time, &time2);
}
