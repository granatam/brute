#ifndef LOG_H
#define LOG_H

#include "common.h"

typedef enum log_level_t
{
  LL_TRACE,
  LL_DEBUG,
  LL_INFO,
  LL_WARN,
  LL_ERROR,
  LL_FATAL,
  LL_OFF,
  LL_UNKNOWN,
} log_level_t;

#define LOG_MESSAGE(log_level, ...)                                           \
  message_impl (log_level, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define IGNORE_MESSAGE(...)

#define TRACE_TRACE LOG_MESSAGE
#define TRACE_DEBUG IGNORE_MESSAGE
#define TRACE_INFO IGNORE_MESSAGE
#define TRACE_WARN IGNORE_MESSAGE
#define TRACE_ERROR IGNORE_MESSAGE
#define TRACE_FATAL IGNORE_MESSAGE
#define TRACE_OFF IGNORE_MESSAGE

#define DEBUG_TRACE LOG_MESSAGE
#define DEBUG_DEBUG LOG_MESSAGE
#define DEBUG_INFO IGNORE_MESSAGE
#define DEBUG_WARN IGNORE_MESSAGE
#define DEBUG_ERROR IGNORE_MESSAGE
#define DEBUG_FATAL IGNORE_MESSAGE
#define DEBUG_OFF IGNORE_MESSAGE

#define INFO_TRACE LOG_MESSAGE
#define INFO_DEBUG LOG_MESSAGE
#define INFO_INFO LOG_MESSAGE
#define INFO_WARN IGNORE_MESSAGE
#define INFO_ERROR IGNORE_MESSAGE
#define INFO_FATAL IGNORE_MESSAGE
#define INFO_OFF IGNORE_MESSAGE

#define WARN_TRACE LOG_MESSAGE
#define WARN_DEBUG LOG_MESSAGE
#define WARN_INFO LOG_MESSAGE
#define WARN_WARN LOG_MESSAGE
#define WARN_ERROR IGNORE_MESSAGE
#define WARN_FATAL IGNORE_MESSAGE
#define WARN_OFF IGNORE_MESSAGE

#define ERROR_TRACE LOG_MESSAGE
#define ERROR_DEBUG LOG_MESSAGE
#define ERROR_INFO LOG_MESSAGE
#define ERROR_WARN LOG_MESSAGE
#define ERROR_ERROR LOG_MESSAGE
#define ERROR_FATAL IGNORE_MESSAGE
#define ERROR_OFF IGNORE_MESSAGE

#define FATAL_TRACE LOG_MESSAGE
#define FATAL_DEBUG LOG_MESSAGE
#define FATAL_INFO LOG_MESSAGE
#define FATAL_WARN LOG_MESSAGE
#define FATAL_ERROR LOG_MESSAGE
#define FATAL_FATAL LOG_MESSAGE
#define FATAL_OFF IGNORE_MESSAGE

#define PASTE2(...) PASTE2_ (__VA_ARGS__)
#define PASTE2_(_0, _1) _0##_##_1

#ifndef LOG_LEVEL
#define LOG_LEVEL ERROR
#endif

#define trace(...) PASTE2 (TRACE, LOG_LEVEL) (LL_TRACE, __VA_ARGS__)
#define debug(...) PASTE2 (DEBUG, LOG_LEVEL) (LL_DEBUG, __VA_ARGS__)
#define info(...) PASTE2 (INFO, LOG_LEVEL) (LL_INFO, __VA_ARGS__)
#define warn(...) PASTE2 (WARN, LOG_LEVEL) (LL_WARN, __VA_ARGS__)
#define error(...) PASTE2 (ERROR, LOG_LEVEL) (LL_ERROR, __VA_ARGS__)
#define fatal(...) PASTE2 (FATAL, LOG_LEVEL) (LL_FATAL, __VA_ARGS__)

status_t message_impl (log_level_t log_level, const char *file_name,
                       const char *func_name, int line, const char *msg, ...);

#endif /* LOG_H */
