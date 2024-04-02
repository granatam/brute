#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#define print_error(...) print_error_impl (__func__, __LINE__, __VA_ARGS__)

#define MAX_PASSWORD_LENGTH (7)
#define HASH_LENGTH (14)

typedef char password_t[MAX_PASSWORD_LENGTH + 1];

typedef struct task_t
{
  password_t password;
  int from;
  int to;
} task_t;

typedef bool (*password_handler_t) (task_t *task, void *context);

typedef enum status_t
{
  S_SUCCESS,
  S_FAILURE,
} status_t;

typedef struct base_state_t
{
  task_t *task;
} base_state_t;

typedef enum command_t
{
  CMD_ALPH,
  CMD_HASH,
  CMD_TASK,
  CMD_EXIT,
} command_t;

status_t print_error_impl (const char *func_name, int line, const char *msg,
                           ...);
void cleanup_mutex_unlock (void *mutex);

status_t recv_wrapper (int socket_fd, void *buf, int len, int flags);
status_t send_wrapper (int socket_fd, void *buf, int len, int flags);

#endif // COMMON_H
