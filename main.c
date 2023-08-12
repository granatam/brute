#include <stdio.h>
#include <string.h>

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

void brute_rec_wrapper (char *password, char *alph, int length)
{
  brute_rec (password, alph, length, 0);
}

int
main (void)
{
  char *alphabet = "abc";
  int length = 3;
  char password[length + 1];
  password[length] = '\0';
  brute_rec_wrapper (password, alphabet, length);
}
