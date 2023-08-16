#include <stdio.h>
#include <string.h>

typedef enum {
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
main (void)
{
  char *alphabet = "abc";
  int length = 3;
  char password[length + 1];
  password[length] = '\0';
  /* brute_rec_wrapper (password, alphabet, length); */
  brute_iter (password, alphabet, length);
}
