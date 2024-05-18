#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#define MAX_PASSWORD_LENGTH (7)
#define MAX_ALPH_LENGTH (20)
#define HASH_LENGTH (14)

typedef char password_t[MAX_PASSWORD_LENGTH + 1];

typedef struct result_t
{
  size_t id;
  password_t password;
  bool is_correct;
} result_t;

typedef struct task_t
{
  result_t task;
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
} command_t;

void cleanup_mutex_unlock (void *mutex);

status_t recv_wrapper (int socket_fd, void *buf, int len, int flags);
status_t send_wrapper (int socket_fd, void *buf, int len, int flags);

#endif // COMMON_H
