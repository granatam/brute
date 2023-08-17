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

void
brute_rec (char *password, char *alph, int length, int pos)
{
  if (pos == length)
    {
      printf ("%s\n", password);
    }
  else
    {
      for (size_t i = 0; alph[i] != '\0'; ++i)
        {
          password[pos] = alph[i];
          brute_rec (password, alph, length, pos + 1);
        }
    }
}

void
brute_rec_wrapper (char *password, char *alph, int length)
{
  brute_rec (password, alph, length, 0);
}

void
brute_iter (char *password, char *alph, int length)
{
  int alph_size = strlen (alph) - 1;
  int idx[length];
  memset (idx, 0, length * sizeof (int));
  memset (password, alph[0], length);

  int pos;
  while (!0)
    {
      printf ("%s\n", password);
      for (pos = length - 1; pos >= 0 && idx[pos] == alph_size; --pos)
        {
          idx[pos] = 0;
          password[pos] = alph[idx[pos]];
        }
      if (pos < 0)
        {
          break;
        }
      password[pos] = alph[++idx[pos]];
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
      brute_iter (password, config.alph, config.length);
      break;
    case BM_RECU:
      brute_rec_wrapper (password, config.alph, config.length);
      break;
    }

  return EXIT_SUCCESS;
}
