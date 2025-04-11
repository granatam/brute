#include "async_client.h"
#include "async_server.h"
#include "common.h"
#include "config.h"
#include "gen.h"
#include "log.h"
#include "multi.h"
#include "reactor_server.h"
#include "single.h"
#include "sync_client.h"
#include "sync_server.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage (char *first_arg)
{
  fprintf (stderr,
           "usage: %s [options] [run mode] [brute mode]\n"
           "options:\n"
           "\t-l, --length uint        password length\n"
           "\t-a, --alph str           alphabet\n"
           "\t-h, --hash str           hash\n"
           "\t-t, --threads uint       number of threads\n"
           "\t-p, --port uint          server port\n"
           "\t-A, --addr str           server address\n"
           "\t-T, --timeout uint       timeout between task receiving and its "
           "processing in milliseconds\n"
           "run modes:\n"
           "\t-s, --single             singlethreaded mode\n"
           "\t-m, --multi              multithreaded mode\n"
           "\t-g, --gen                generator mode\n"
           "\t-c, --client             synchronous client mode\n"
           "\t-L, --load-clients uint  spawn N load clients\n"
           "\t-S, --server             synchronous server mode\n"
           "\t-v, --async-client       asynchronous client mode\n"
           "\t-w, --async-server       asynchronous server mode\n"
           "\t-R, --reactor-server     reactor server mode\n"
           "brute modes:\n"
           "\t-i, --iter               iterative bruteforce\n"
           "\t-r, --rec                recursive bruteforce\n"
#ifndef __APPLE__
           "\t-y, --rec-gen            recursive generator\n"
#endif
           ,
           first_arg);
}
static status_t
parse_params (config_t *config, int argc, char *argv[])
{
  const char short_opts[] = "l:a:H:t:p:A:L:T:smgcSvwRiryh";
  const struct option long_opts[]
      = { { "length", required_argument, 0, 'l' },
          { "alph", required_argument, 0, 'a' },
          { "hash", required_argument, 0, 'H' },
          { "threads", required_argument, 0, 't' },
          { "port", required_argument, 0, 'p' },
          { "addr", required_argument, 0, 'A' },
          { "load-clients", required_argument, 0, 'L' },
          { "timeout", required_argument, 0, 'T' },
          { "single", no_argument, 0, 's' },
          { "multi", no_argument, 0, 'm' },
          { "gen", no_argument, 0, 'g' },
          { "client", no_argument, 0, 'c' },
          { "server", no_argument, 0, 'S' },
          { "async-client", no_argument, 0, 'v' },
          { "async-server", no_argument, 0, 'w' },
          { "reactor-server", no_argument, 0, 'R' },
          { "iter", no_argument, 0, 'i' },
          { "rec", no_argument, 0, 'r' },
          { "rec-gen", no_argument, 0, 'y' },
          { "help", no_argument, 0, 'h' },
          { NULL, 0, NULL, 0 } };

  int opt = 0;
  while ((opt = getopt_long (argc, argv, short_opts, long_opts, NULL)) != -1)
    {
      switch (opt)
        {
        case 'l':
          config->length = atoi (optarg);
          if (config->length <= 0 || config->length > MAX_PASSWORD_LENGTH)
            {
              error ("Password's length must be a number between 0 and %d",
                     MAX_PASSWORD_LENGTH);
              return (S_FAILURE);
            }
          break;
        case 'a':
          config->alph = optarg;
          if (strlen (config->alph) <= 0
              || strlen (config->alph) > MAX_ALPH_LENGTH)
            {
              error ("Alphabet's length must be between 0 and %d",
                     MAX_ALPH_LENGTH);
              return (S_FAILURE);
            }
          break;
        case 'H':
          config->hash = optarg;
          break;
        case 't':
          {
            int number_of_cpus = sysconf (_SC_NPROCESSORS_ONLN);
            config->number_of_threads = atoi (optarg);
            if (config->number_of_threads < 1
                || config->number_of_threads > number_of_cpus)
              {
                error ("Number of threads must be a number between 1 and %d",
                       number_of_cpus);
                return (S_FAILURE);
              }
            break;
          }
        case 'p':
          config->port = atoi (optarg);
          if (config->length <= 0 || config->length > MAX_TCP_PORT)
            {
              error ("Port must be a number between 0 and %d", MAX_TCP_PORT);
              return (S_FAILURE);
            }
          break;
        case 'A':
          config->addr = optarg;
          break;
        case 'T':
          config->timeout = atoi (optarg);
          if (config->timeout < 0)
            {
              error ("Timeout must be a number greater than 0");
              return (S_FAILURE);
            }
          break;
        case 's':
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
        case 'L':
          config->number_of_threads = atoi (optarg);
          if (config->number_of_threads < 1)
            {
              error ("Number of load clients to spawn must be a number "
                     "greater than 1");
              return (S_FAILURE);
            }
          config->run_mode = RM_LOAD_CLIENT;
          break;
        case 'S':
          config->run_mode = RM_SERVER;
          break;
        case 'v':
          config->run_mode = RM_ASYNC_CLIENT;
          break;
        case 'w':
          config->run_mode = RM_ASYNC_SERVER;
          break;
        case 'R':
          config->run_mode = RM_REACTOR_SERVER;
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
        case 'h':
          usage (argv[0]);
          exit (EXIT_SUCCESS);
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
    .timeout = 0,
  };

  if (parse_params (&config, argc, argv) == S_FAILURE)
    {
      usage (argv[0]);
      return (EXIT_FAILURE);
    }

  task_t task;
  memset (&task, 0, sizeof (task));

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
    case RM_ASYNC_SERVER:
      is_found = run_async_server (&task, &config);
      break;
    case RM_SERVER:
      is_found = run_server (&task, &config);
      break;
    case RM_ASYNC_CLIENT:
      run_async_client (&config);
      break;
    case RM_CLIENT:
      run_client (&config, sync_client_find_password);
      break;
    case RM_LOAD_CLIENT:
      spawn_clients (&config, NULL);
      break;
    case RM_REACTOR_SERVER:
      is_found = run_reactor_server (&task, &config);
      break;
    }

  /* Clients should not output anything, only computations and data exchange
   * with the server */
  if (config.run_mode == RM_CLIENT || config.run_mode == RM_LOAD_CLIENT
      || config.run_mode == RM_ASYNC_CLIENT)
    return (EXIT_SUCCESS);

  if (is_found)
    printf ("Password found: %s\n", task.result.password);
  else
    printf ("Password not found\n");

  return (EXIT_SUCCESS);
}
