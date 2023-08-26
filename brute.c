#include "brute.h"

#include <stdlib.h>
#include <string.h>

bool
brute_rec (char *password, config_t *config,
           password_handler_t password_handler, void *context, int pos)
{
  if (pos == config->length)
    {
      return password_handler (password, context);
    }
  else
    {
      for (size_t i = 0; config->alph[i] != '\0'; ++i)
        {
          password[pos] = config->alph[i];
          if (brute_rec (password, config, password_handler, context, pos + 1))
            {
              return true;
            }
        }
    }
  return false;
}

bool
brute_rec_wrapper (char *password, config_t *config,
                   password_handler_t password_handler, void *context)
{
  return brute_rec (password, config, password_handler, context, 0);
}

bool
brute_iter (char *password, config_t *config,
            password_handler_t password_handler, void *context)
{
  int alph_size = strlen (config->alph) - 1;
  int idx[config->length];
  memset (idx, 0, config->length * sizeof (int));
  memset (password, config->alph[0], config->length);

  int pos;
  while (true)
    {
      if (password_handler (password, context))
        {
          return true;
        }
      for (pos = config->length - 1; pos >= 0 && idx[pos] == alph_size; --pos)
        {
          idx[pos] = 0;
          password[pos] = config->alph[idx[pos]];
        }
      if (pos < 0)
        {
          return false;
        }
      password[pos] = config->alph[++idx[pos]];
    }
}
