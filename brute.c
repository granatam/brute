#include "brute.h"

#include "iter.h"
#include "rec.h"

bool
brute (task_t *task, config_t *config, password_handler_t password_handler,
       void *context)
{
  bool is_found = false;
  // TODO: we could pass config->alph instead of config here
  switch (config->brute_mode)
    {
    case BM_REC_GEN:
#ifndef __APPLE__
      is_found = brute_rec_gen (task, config, password_handler, context);
      break;
#endif
    case BM_ITER:
      is_found = brute_iter (task, config, password_handler, context);
      break;
    case BM_RECU:
      is_found = brute_rec_wrapper (task, config, password_handler, context);
      break;
    }

  return (is_found);
}
