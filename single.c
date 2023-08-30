#include "single.h"

#include "brute.h"
#include <string.h>
#ifdef __APPLE__
#include <unistd.h>
#else
#include <crypt.h>
#endif

bool
st_password_handler (task_t *task, void *context)
{
  char *hash = (char *)context;
  char *hashed_password = crypt (task->password, hash);

  return strcmp (hash, hashed_password) == 0;
}

bool
run_single (task_t *task, config_t *config)
{
  bool is_found = false;
  switch (config->brute_mode)
    {
    case BM_ITER:
      is_found = brute_iter (task, config, st_password_handler, config->hash);
      break;
    case BM_RECU:
      is_found = brute_rec_wrapper (task, config, st_password_handler,
                                    config->hash);
      break;
    }

  return is_found;
}
