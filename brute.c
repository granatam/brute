#include "brute.h"

#include <stdlib.h>
#include <string.h>

bool
brute_rec (task_t *task, config_t *config, password_handler_t password_handler,
           void *context, int pos)
{
  if (pos == config->length)
    return password_handler (task, context);
  else
    {
      for (size_t i = 0; config->alph[i] != '\0'; ++i)
        {
          task->password[pos] = config->alph[i];

          if (brute_rec (task, config, password_handler, context, pos + 1))
            return true;
        }
    }
  return false;
}

bool
brute_rec_wrapper (task_t *task, config_t *config,
                   password_handler_t password_handler, void *context)
{
  return brute_rec (task, config, password_handler, context, 0);
}

bool
brute_iter (task_t *task, config_t *config,
            password_handler_t password_handler, void *context)
{
  int alph_size = strlen (config->alph) - 1;
  int idx[config->length];
  memset (idx, 0, config->length * sizeof (int));
  memset (task->password, config->alph[0], config->length);

  int pos;
  while (true)
    {
      if (password_handler (task, context))
        return true;

      for (pos = config->length - 1; pos >= 0 && idx[pos] == alph_size; --pos)
        {
          idx[pos] = 0;
          task->password[pos] = config->alph[idx[pos]];
        }

      if (pos < 0)
        return false;

      task->password[pos] = config->alph[++idx[pos]];
    }
}

bool
brute (task_t *task, config_t *config, password_handler_t password_handler,
       void *context)
{
  bool is_found = false;
  switch (config->brute_mode)
    {
    case BM_ITER:
      is_found = brute_iter (task, config, password_handler, context);
      break;
    case BM_RECU:
      is_found = brute_rec_wrapper (task, config, password_handler, context);
      break;
    }

  return is_found;
}
