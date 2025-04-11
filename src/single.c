#include "single.h"

#include "brute.h"
#include "common.h"

#include <string.h>

bool
st_password_check (task_t *task, void *context)
{
  st_context_t *st_ctx = context;
  char *hashed_password
      = crypt_r (task->result.password, st_ctx->hash, &st_ctx->data);

  return (strcmp (st_ctx->hash, hashed_password) == 0);
}

bool
run_single (task_t *task, config_t *config)
{
  st_context_t context = {
    .hash = config->hash,
    .data = { .initialized = 0 },
  };

  task->from = 0;
  task->to = config->length;

  return (brute (task, config, st_password_check, &context));
}
