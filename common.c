#include "common.h"

#include <stdarg.h>
#include <stdio.h>

__attribute__ ((format (printf, 1, 2))) status_t
print_error (const char *msg, ...)
{
  va_list args;
  va_start (args, msg);

  if (vfprintf (stderr, msg, args) != 0)
    return S_FAILURE;

  va_end (args);

  return S_SUCCESS;
}

void
cleanup_mutex_unlock (void *mutex)
{
  pthread_mutex_unlock ((pthread_mutex_t *)mutex);
}
