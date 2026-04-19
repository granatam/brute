#include "common.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)                                        \
  (__extension__ ({                                                           \
    long int __result;                                                        \
    do                                                                        \
      __result = (long int)(expression);                                      \
    while (__result == -1L && errno == EINTR);                                \
    __result;                                                                 \
  }))
#endif

void
cleanup_mutex_unlock (void *mutex)
{
  pthread_mutex_unlock ((pthread_mutex_t *)mutex);
}

io_status_t
recv_wrapper (int socket_fd, void *buf, size_t len, int flags)
{
  char *bytes = buf;
  while (len > 0)
    {
      ssize_t read = TEMP_FAILURE_RETRY (recv (socket_fd, bytes, len, flags));
      if (read < 0)
        return (IOS_FAILURE);
      if (read == 0)
        return (IOS_CONN_CLOSED);

      size_t bytes_read = read;
      len -= bytes_read;
      bytes += bytes_read;
    }

  return (IOS_SUCCESS);
}

io_status_t
send_wrapper (int socket_fd, struct iovec *vec, int iovcnt)
{
  while (iovcnt > 0)
    {
      ssize_t written = TEMP_FAILURE_RETRY (writev (socket_fd, vec, iovcnt));
      if (written < 0)
        return (IOS_FAILURE);
      if (written == 0)
        return (IOS_CONN_CLOSED);

      size_t bytes_written = written;
      while (bytes_written > 0)
        {
          if (bytes_written >= vec[0].iov_len)
            {
              bytes_written -= vec[0].iov_len;
              ++vec;
              --iovcnt;
            }
          else
            {
              vec[0].iov_len -= bytes_written;
              char *base = vec[0].iov_base;
              vec[0].iov_base = base + bytes_written;
              break;
            }
        }
    }

  return (IOS_SUCCESS);
}
