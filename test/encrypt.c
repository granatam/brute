#include <stdio.h>
#include <stdlib.h>
#include <crypt.h>
#include <unistd.h>

int main(int argc, char **argv) 
{
  char *password = "abc";
  char *salt = "abc";
  int opt = 0;
  while ((opt = getopt (argc, argv, "p:s:")) != -1)
    {
      switch (opt)
        {
        case 'p':
          password = optarg;
          break;
        case 's':
          salt = optarg;
          break;
        default:
          return (EXIT_FAILURE);
        }
    }

  struct crypt_data data;
  data.initialized = 0;

  printf ("%s\n", crypt_r (password, salt, &data));

  return (EXIT_SUCCESS);
}