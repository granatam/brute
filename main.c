#include <stdio.h>
#include <string.h>

void
bruteforce (char *alphabet)
{
  size_t alph_size = strlen (alphabet);
  for (size_t i = 0; i < alph_size; ++i)
    {
      for (size_t j = 0; j < alph_size; ++j)
        {
          for (size_t k = 0; k < alph_size; ++k)
            {
              printf ("%c%c%c\n", alphabet[i], alphabet[j], alphabet[k]);
            }
        }
    }
}

int
main (void)
{
  char *alphabet = "abc";
  bruteforce (alphabet);
}
