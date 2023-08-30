#include "brute.h"
#include "common.h"
#include "config.h"
#include "multi.h"
#include "queue.h"
#include "single.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

status_t
parse_params (config_t *config, int argc, char *argv[])
{
  int opt = 0;
  while ((opt = getopt (argc, argv, "l:a:h:smir")) != -1)
    {
      switch (opt)
        {
        case 'l':
          config->length = atoi (optarg);
          if (config->length <= 0 || config->length > MAX_PASSWORD_LENGTH)
            {
              print_error (
                  "Password's length must be a number between 0 and %d\n",
                  MAX_PASSWORD_LENGTH);
              return S_FAILURE;
            }
          break;
        case 'a':
          config->alph = optarg;
          break;
        case 'h':
          config->hash = optarg;
          break;
        case 's':
          config->run_mode = RM_SINGLE;
          break;
        case 'm':
          config->run_mode = RM_MULTI;
          break;
        case 'i':
          config->brute_mode = BM_ITER;
          break;
        case 'r':
          config->brute_mode = BM_RECU;
          break;
        default:
          return S_FAILURE;
        }
    }

  return S_SUCCESS;
}

int
main (int argc, char *argv[])
{
  config_t config = {
    .run_mode = RM_SINGLE,
    .brute_mode = BM_ITER,
    .length = 3,
    .alph = "abc",
    .hash = "abFZSxKKdq5s6", /* crypt ("abc", "abc"); */
  };
  if (parse_params (&config, argc, argv) == S_FAILURE)
    {
      return EXIT_FAILURE;
    }

  task_t task;
  task.password[config.length] = '\0';

  bool is_found = false;
  switch (config.run_mode)
    {
    case RM_SINGLE:
      is_found = run_single (&task, &config);
      break;
    case RM_MULTI:
      is_found = run_multi (&task, &config);
      break;
    }

  if (is_found)
    {
      printf ("Password found: %s\n", task.password);
    }
  else
    {
      printf ("Password not found\n");
    }

  return EXIT_SUCCESS;
}
