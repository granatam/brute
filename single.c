#include "single.h"

#include "brute.h"
#include <string.h>

#ifdef __APPLE__
#define crypt_r __crypt_r
#endif

bool
st_password_check (task_t *task, void *context)
{
  st_context_t *st_context = (st_context_t *)context;
  char *hashed_password
      = crypt_r (task->password, st_context->hash, &st_context->data);

  return (strcmp (st_context->hash, hashed_password) == 0);
}

bool
run_single (task_t *task, config_t *config)
{
  st_context_t context = {
    .hash = config->hash,
    .data = { .initialized = 0 },
  };

  return (brute (task, config, st_password_check, &context));
}
