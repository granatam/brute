#include "brute.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "gen.h"
#include "multi.h"
#include "queue.h"
#include "server.h"
#include "single.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static status_t
parse_params (config_t *config, int argc, char *argv[])
{
  int opt = 0;
  while ((opt = getopt (argc, argv, "l:a:h:t:p:b:dmgcsiry")) != -1)
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
              return (S_FAILURE);
            }
          break;
        case 'a':
          config->alph = optarg;
          break;
        case 'h':
          config->hash = optarg;
          break;
        case 't':
          {
            int number_of_cpus = sysconf (_SC_NPROCESSORS_ONLN);
            config->number_of_threads = atoi (optarg);
            if (config->number_of_threads < 1
                || config->number_of_threads > number_of_cpus)
              {
                print_error (
                    "Number of threads must be a number between 1 and %d",
                    number_of_cpus);
                return (S_FAILURE);
              }
            break;
          }
        case 'p':
          config->port = atoi (optarg);
          if (config->length <= 0 || config->length > MAX_TCP_PORT)
            {
              print_error ("Port must be a number between 0 and %d\n",
                           MAX_TCP_PORT);
              return (S_FAILURE);
            }
          break;
        case 'b':
          config->addr = optarg;
          break;
        case 'd': /* default mode */
          config->run_mode = RM_SINGLE;
          break;
        case 'm':
          config->run_mode = RM_MULTI;
          break;
        case 'g':
          config->run_mode = RM_GENERATOR;
          break;
        case 'c':
          config->run_mode = RM_CLIENT;
          break;
        case 's':
          config->run_mode = RM_SERVER;
          break;
        case 'i':
          config->brute_mode = BM_ITER;
          break;
        case 'r':
          config->brute_mode = BM_RECU;
          break;
        case 'y':
          config->brute_mode = BM_REC_GEN;
          break;
        default:
          return (S_FAILURE);
        }
    }

  return (S_SUCCESS);
}

int
main (int argc, char *argv[])
{
  config_t config = {
    .run_mode = RM_SINGLE,
    .brute_mode = BM_ITER,
    .length = 3,
    .number_of_threads = sysconf (_SC_NPROCESSORS_ONLN),
    .alph = "abc",
    .hash = "abFZSxKKdq5s6", /* crypt ("abc", "abc"); */
    .port = 9000,
    .addr = "127.0.0.1",
  };

  if (parse_params (&config, argc, argv) == S_FAILURE)
    return (EXIT_FAILURE);

  task_t task;
  memset (task.password, 0, sizeof (task.password));

  bool is_found = false;
  switch (config.run_mode)
    {
    case RM_SINGLE:
      is_found = run_single (&task, &config);
      break;
    case RM_MULTI:
      is_found = run_multi (&task, &config);
      break;
    case RM_GENERATOR:
      is_found = run_generator (&task, &config);
      break;
    case RM_SERVER:
      is_found = run_server (&task, &config);
      break;
    case RM_CLIENT:
      is_found = run_client (&task, &config);
      break;
    }

  if (is_found)
    printf ("Password found: %s\n", task.password);
  else
    printf ("Password not found\n");

  return (EXIT_SUCCESS);
}
