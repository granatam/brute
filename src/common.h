#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#include <sys/uio.h>

#define MAX_PASSWORD_LENGTH (7)
#define MAX_ALPH_LENGTH (20)
#define HASH_LENGTH (14)

typedef char password_t[MAX_PASSWORD_LENGTH + 1];

typedef struct result_t
{
  int id;
  bool is_correct;
  password_t password;
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
status_t send_wrapper (int socket_fd, struct iovec *vec, int iovcnt);

status_t recv_wrapper_nonblock (int socket_fd, void *buf, int len, int flags,
                                int *total);
status_t send_wrapper_nonblock (int socket_fd, struct iovec *vec, int iovcnt,
                                size_t *total);

#endif // COMMON_H
