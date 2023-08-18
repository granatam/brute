#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum
{
  BM_ITER,
  BM_RECU,
} brute_mode_t;

typedef struct config_t
{
  int length;
  char *alph;
  brute_mode_t brute_mode;
} config_t;

typedef bool (*password_handler_t) (char *password, void *context);

bool
password_handler (char *password, void *context)
{
  (void)context; /* just to temporary suppress "unused parameter" warning*/
  printf ("%s\n", password);
  return false;
}

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

int
parse_params (config_t *config, int argc, char *argv[])
{
  int opt = 0;
  while ((opt = getopt (argc, argv, "l:a:ir")) != -1)
    {
      switch (opt)
        {
        case 'l':
          config->length = atoi (optarg);
          if (config->length == 0)
            {
              printf ("Password's length must be a number greater than 0\n");
              return -1;
            }
          break;
        case 'a':
          config->alph = optarg;
          break;
        case 'i':
          config->brute_mode = BM_ITER;
          break;
        case 'r':
          config->brute_mode = BM_RECU;
          break;
        default:
          return -1;
        }
    }

  return 0;
}

int
main (int argc, char *argv[])
{
  config_t config = {
    .length = 3,
    .alph = "abc",
    .brute_mode = BM_ITER,
  };
  if (parse_params (&config, argc, argv) == -1)
    {
      return EXIT_FAILURE;
    }

  char password[config.length + 1];
  password[config.length] = '\0';

  switch (config.brute_mode)
    {
    case BM_ITER:
      brute_iter (password, &config, password_handler, NULL);
      break;
    case BM_RECU:
      brute_rec_wrapper (password, &config, password_handler, NULL);
      break;
    }

  return EXIT_SUCCESS;
}
