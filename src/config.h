#ifndef CONFIG_H
#define CONFIG_H

#define MAX_TCP_PORT 65535

typedef enum
{
  RM_SINGLE,
  RM_MULTI,
  RM_GENERATOR,
  RM_CLIENT,
  RM_SERVER,
  RM_LOAD_CLIENT,
  RM_ASYNC_CLIENT,
  RM_ASYNC_SERVER,
  RM_REACTOR_SERVER,
} run_mode_t;

typedef enum
{
  BM_ITER,
  BM_RECU,
  BM_REC_GEN,
} brute_mode_t;

typedef struct config_t
{
  run_mode_t run_mode;
  brute_mode_t brute_mode;
  int number_of_threads;
  int length;
  char *alph;
  char *hash;
  char *addr;
  int port;
  int timeout;
} config_t;

#endif // CONFIG_H
