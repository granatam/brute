#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#define MAX_PASSWORD_LENGTH (7)

typedef char password_t[MAX_PASSWORD_LENGTH + 1];

typedef struct task_t
{
  password_t password;
} task_t;

typedef bool (*password_handler_t) (task_t *task, void *context);

typedef enum status_t
{
  S_SUCCESS,
  S_FAILURE,
} status_t;

status_t print_error (const char *msg, ...);
void cleanup_mutex_unlock (void *mutex);

#endif // COMMON_H
