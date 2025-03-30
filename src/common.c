#include "common.h"

#define __USE_GNU
#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>
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

status_t
recv_wrapper (int socket_fd, void *buf, int len, int flags)
{
  char *bytes = buf;
  while (len > 0)
    {
      int bytes_read
          = TEMP_FAILURE_RETRY (recv (socket_fd, bytes, len, flags));
      if (bytes_read <= 0)
        return (S_FAILURE);
      len -= bytes_read;
      bytes += bytes_read;
    }

  return (S_SUCCESS);
}

status_t
send_wrapper (int socket_fd, struct iovec *vec, int iovcnt)
{
  while (iovcnt > 0)
    {
      size_t bytes_written
          = TEMP_FAILURE_RETRY (writev (socket_fd, vec, iovcnt));
      if ((int)bytes_written <= 0)
        return (S_FAILURE);

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

  return (S_SUCCESS);
}

status_t
recv_wrapper_nonblock (int socket_fd, void *buf, int len, int flags,
                       int *total)
{
  char *bytes = buf;
  while (len > 0)
    {
      int bytes_read = recv (socket_fd, bytes, len, flags);
      if (bytes_read <= 0)
        {
          *total = bytes_read;
          return (S_FAILURE);
        }
      *total += bytes_read;
      len -= bytes_read;
      bytes += bytes_read;
    }

  return (S_SUCCESS);
}

status_t
send_wrapper_nonblock (int socket_fd, struct iovec *vec, int iovcnt,
                       int *total)
{
  while (iovcnt > 0)
    {
      size_t bytes_written = writev (socket_fd, vec, iovcnt);
      if ((int)bytes_written <= 0)
        return (S_FAILURE);

      *total += bytes_written;
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

  return (S_SUCCESS);
}
