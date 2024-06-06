#ifndef LOG_H
#define LOG_H

#include "common.h"

#define LOG_MESSAGE(log_level, ...)                                           \
  message_impl (log_level, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define IGNORE_MESSAGE(...)

#define LOG_TRACE(...) LOG_MESSAGE ("TRACE", __VA_ARGS__)
#define LOG_DEBUG(...) LOG_MESSAGE ("DEBUG", __VA_ARGS__)
#define LOG_INFO(...) LOG_MESSAGE ("INFO", __VA_ARGS__)
#define LOG_WARN(...) LOG_MESSAGE ("WARNING", __VA_ARGS__)
#define LOG_ERROR(...) LOG_MESSAGE ("ERROR", __VA_ARGS__)
#define LOG_FATAL(...) LOG_MESSAGE ("FATAL", __VA_ARGS__)

#define TRACE_TRACE LOG_TRACE
#define TRACE_DEBUG IGNORE_MESSAGE
#define TRACE_INFO IGNORE_MESSAGE
#define TRACE_WARN IGNORE_MESSAGE
#define TRACE_ERROR IGNORE_MESSAGE
#define TRACE_FATAL IGNORE_MESSAGE
#define TRACE_OFF IGNORE_MESSAGE

#define DEBUG_TRACE LOG_DEBUG
#define DEBUG_DEBUG LOG_DEBUG
#define DEBUG_INFO IGNORE_MESSAGE
#define DEBUG_WARN IGNORE_MESSAGE
#define DEBUG_ERROR IGNORE_MESSAGE
#define DEBUG_FATAL IGNORE_MESSAGE
#define DEBUG_OFF IGNORE_MESSAGE

#define INFO_TRACE LOG_INFO
#define INFO_DEBUG LOG_INFO
#define INFO_INFO LOG_INFO
#define INFO_WARN IGNORE_MESSAGE
#define INFO_ERROR IGNORE_MESSAGE
#define INFO_FATAL IGNORE_MESSAGE
#define INFO_OFF IGNORE_MESSAGE

#define WARN_TRACE LOG_WARN
#define WARN_DEBUG LOG_WARN
#define WARN_INFO LOG_WARN
#define WARN_WARN LOG_WARN
#define WARN_ERROR IGNORE_MESSAGE
#define WARN_FATAL IGNORE_MESSAGE
#define WARN_OFF IGNORE_MESSAGE

#define ERROR_TRACE LOG_ERROR
#define ERROR_DEBUG LOG_ERROR
#define ERROR_INFO LOG_ERROR
#define ERROR_WARN LOG_ERROR
#define ERROR_ERROR LOG_ERROR
#define ERROR_FATAL IGNORE_MESSAGE
#define ERROR_OFF IGNORE_MESSAGE

#define FATAL_TRACE LOG_FATAL
#define FATAL_DEBUG LOG_FATAL
#define FATAL_INFO LOG_FATAL
#define FATAL_WARN LOG_FATAL
#define FATAL_ERROR LOG_FATAL
#define FATAL_FATAL LOG_FATAL
#define FATAL_OFF IGNORE_MESSAGE

#define PASTE2(...) PASTE2_ (__VA_ARGS__)
#define PASTE2_(_0, _1) _0##_##_1

#ifndef LOG_LEVEL
#define LOG_LEVEL ERROR
#endif

#define trace(...) PASTE2 (TRACE, LOG_LEVEL) (__VA_ARGS__)
#define debug(...) PASTE2 (DEBUG, LOG_LEVEL) (__VA_ARGS__)
#define info(...) PASTE2 (INFO, LOG_LEVEL) (__VA_ARGS__)
#define warn(...) PASTE2 (WARN, LOG_LEVEL) (__VA_ARGS__)
#define error(...) PASTE2 (ERROR, LOG_LEVEL) (__VA_ARGS__)
#define fatal(...) PASTE2 (FATAL, LOG_LEVEL) (__VA_ARGS__)

status_t message_impl (const char *log_level, const char *file_name,
                       const char *func_name, int line, const char *msg, ...);

#endif /* LOG_H */
