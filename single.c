#include "single.h"

#include "brute.h"
#include <string.h>
#ifdef __APPLE__
#include <unistd.h>
#else
#include <crypt.h>
#endif

bool
password_handler (password_t password, void *context)
{
  char *hash = (char *)context;
  char *hashed_password = crypt (password, hash);

  return strcmp (hash, hashed_password) == 0;
}

bool
run_single (password_t password, config_t *config)
{
  switch (config->brute_mode)
    {
    case BM_ITER:
      return brute_iter (password, config, password_handler, config->hash);
    case BM_RECU:
      return brute_rec_wrapper (password, config, password_handler,
                                config->hash);
    }
}
