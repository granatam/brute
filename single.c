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
  switch (config->brute_mode)
    {
    case BM_ITER:
      return brute_iter (task, config, st_password_handler, config->hash);
    case BM_RECU:
      return brute_rec_wrapper (task, config, st_password_handler,
                                config->hash);
    }
}
