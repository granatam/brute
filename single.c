#include "single.h"

#include "brute.h"
#include <crypt.h>
#include <string.h>

bool
st_password_check (task_t *task, void *context)
{
  st_context_t *st_context = (st_context_t *)context;
  char *hashed_password
      = crypt_r (task->password, st_context->hash, st_context->data);

  return strcmp (st_context->hash, hashed_password) == 0;
}

bool
run_single (task_t *task, config_t *config)
{
  struct crypt_data data;
  data.initialized = 0;
  st_context_t st_context = {
    .hash = config->hash,
    .data = &data,
  };

  bool is_found = false;
  switch (config->brute_mode)
    {
    case BM_ITER:
      is_found = brute_iter (task, config, st_password_check, &st_context);
      break;
    case BM_RECU:
      is_found
          = brute_rec_wrapper (task, config, st_password_check, &st_context);
      break;
    }

  return is_found;
}
