#define _GNU_SOURCE
#include "log.h"
#include "common.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

  if (level < log_level)
    return (S_SUCCESS);

  char *log;
  if (asprintf (&log, "(%s %s %d) ", file_name, func_name, line) < 0)
    if (fprintf (stderr, "(%s %s %d) ", file_name, func_name, line) < 0)
      return (S_FAILURE);

  va_list args;
  va_start (args, msg);

  char *message;
  if (vasprintf (&message, msg, args) < 0)
    if (vfprintf (stderr, msg, args) < 0)
      return (S_FAILURE);

  va_end (args);

  fprintf (stderr, "%s %s\n", log, message);
  free (message);
  free (log);

  fflush (stderr);

  return (S_SUCCESS);
}
