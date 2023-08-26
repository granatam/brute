#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

#define MAX_PASSWORD_LENGTH (7)

typedef char password_t[MAX_PASSWORD_LENGTH + 1];

typedef bool (*password_handler_t) (char *password, void *context);

typedef struct task_t
{
  password_t password;
} task_t;

#endif
