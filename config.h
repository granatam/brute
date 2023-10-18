#ifndef CONFIG_H
#define CONFIG_H

typedef enum
{
  RM_SINGLE,
  RM_MULTI,
  RM_GENERATOR,
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
} config_t;

#endif // CONFIG_H
