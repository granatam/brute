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
} log_level_t;

#define trace(...)                                                            \
  message_impl (__FILE__, __func__, __LINE__, LL_TRACE, __VA_ARGS__)
#define debug(...)                                                            \
  message_impl (__FILE__, __func__, __LINE__, LL_DEBUG, __VA_ARGS__)
#define info(...)                                                             \
  message_impl (__FILE__, __func__, __LINE__, LL_INFO, __VA_ARGS__)
#define warn(...)                                                             \
  message_impl (__FILE__, __func__, __LINE__, LL_WARN, __VA_ARGS__)
#define error(...)                                                            \
  message_impl (__FILE__, __func__, __LINE__, LL_ERROR, __VA_ARGS__)
#define fatal(...)                                                            \
  message_impl (__FILE__, __func__, __LINE__, LL_FATAL, __VA_ARGS__)

status_t message_impl (const char *file_name, const char *func_name, int line,
                       log_level_t log_level, const char *msg, ...);

#endif /* LOG_H */
