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

void
brute_rec (char *password, config_t *config, int pos)
{
  if (pos == config->length)
    {
      printf ("%s\n", password);
    }
  else
    {
      for (size_t i = 0; config->alph[i] != '\0'; ++i)
        {
          password[pos] = config->alph[i];
          brute_rec (password, config, pos + 1);
        }
    }
}

void
brute_rec_wrapper (char *password, config_t *config)
{
  brute_rec (password, config, 0);
}

void
brute_iter (char *password, config_t *config)
{
  int alph_size = strlen (config->alph) - 1;
  int idx[config->length];
  memset (idx, 0, config->length * sizeof (int));
  memset (password, config->alph[0], config->length);

  int pos;
  while (!0)
    {
      printf ("%s\n", password);
      for (pos = config->length - 1; pos >= 0 && idx[pos] == alph_size; --pos)
        {
          idx[pos] = 0;
          password[pos] = config->alph[idx[pos]];
        }
      if (pos < 0)
        {
          break;
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
      brute_iter (password, &config);
      break;
    case BM_RECU:
      brute_rec_wrapper (password, &config);
      break;
    }

  return EXIT_SUCCESS;
}
