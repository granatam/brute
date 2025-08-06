#ifndef SINGLE_H
#define SINGLE_H

#include "common.h"
#include "config.h"

#include <stdbool.h>

#include <crypt.h>

typedef struct st_context_t
{
  char *hash;
  struct crypt_data data;
} st_context_t;

bool st_password_check (task_t *task, void *context);
bool run_single (task_t *task, config_t *config);

#endif // SINGLE_H
