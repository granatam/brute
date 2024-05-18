#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

__attribute__ ((format (printf, 5, 6))) status_t
message_impl (const char *file_name, const char *func_name, int line,
              log_level_t level, const char *msg, ...)
{
  static log_level_t log_level = LL_OFF;
#ifdef LOG_LEVEL
#define LOG_LEVEL_MACRO(str) LL_##str
#define EXPAND_LOG_LEVEL_MACRO(str) LOG_LEVEL_MACRO (str)
  log_level = EXPAND_LOG_LEVEL_MACRO (LOG_LEVEL);
#endif

  if (msg == NULL)
    {
      log_level = level;
      return (S_SUCCESS);
    }

  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  if (level < log_level)
    return (S_SUCCESS);

  if (pthread_mutex_lock (&mutex) != 0)
    return (S_FAILURE);

  fprintf (stderr, "(%s %s %d) ", file_name, func_name, line);

  va_list args;
  va_start (args, msg);

  int vfprintf_result = vfprintf (stderr, msg, args);
  va_end (args);

  fflush(stderr);

  if (pthread_mutex_unlock (&mutex) != 0)
    return (S_FAILURE);

  if (vfprintf_result < 0)
    return (S_FAILURE);

  return (S_SUCCESS);
}
