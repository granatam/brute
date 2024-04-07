#include "common.h"

#define __USE_GNU
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
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

__attribute__ ((format (printf, 3, 4))) status_t
print_error_impl (const char *func_name, int line, const char *msg, ...)
{
  fprintf (stderr, "(%s %d) ", func_name, line);

  va_list args;
  va_start (args, msg);

  if (vfprintf (stderr, msg, args) != 0)
    return (S_FAILURE);

  va_end (args);

  return (S_SUCCESS);
}

void
cleanup_mutex_unlock (void *mutex)
{
  pthread_mutex_unlock ((pthread_mutex_t *)mutex);
}

status_t
recv_wrapper (int socket_fd, void *buf, int len, int flags)
{
  char *bytes = (char *)buf;
  while (len > 0)
    {
      int bytes_read
          = TEMP_FAILURE_RETRY (recv (socket_fd, bytes, len, flags));
      if (bytes_read == -1 || bytes_read == 0)
        return (S_FAILURE);
      len -= bytes_read;
      bytes += bytes_read;
    }

  return (S_SUCCESS);
}

status_t
send_wrapper (int socket_fd, void *buf, int len, int flags)
{
  char *bytes = (char *)buf;
  while (len > 0)
    {
      int bytes_written
          = TEMP_FAILURE_RETRY (send (socket_fd, bytes, len, flags));
      if (bytes_written == -1 || bytes_written == 0)
        return (S_FAILURE);
      len -= bytes_written;
      bytes += bytes_written;
    }

  return (S_SUCCESS);
}
