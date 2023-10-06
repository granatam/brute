#include "rec.h"

bool
brute_rec (task_t *task, config_t *config, password_handler_t password_handler,
           void *context, int pos)
{
  if (pos == task->to)
    return (password_handler (task, context));
  else
    {
      for (size_t i = 0; config->alph[i] != '\0'; ++i)
        {
          task->password[pos] = config->alph[i];

          if (brute_rec (task, config, password_handler, context, pos + 1))
            return (true);
        }
    }
  return (false);
}

bool
brute_rec_wrapper (task_t *task, config_t *config,
                   password_handler_t password_handler, void *context)
{
  return (brute_rec (task, config, password_handler, context, task->from));
}
