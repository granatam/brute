#ifndef GEN_H
#define GEN_H

#include "iter.h"
#include "config.h"
#include <pthread.h>

typedef struct gen_context_t
{
  iter_state_t state;
  pthread_mutex_t mutex;
  config_t *config;
  bool cancelled;
} gen_context_t;

bool run_generator (task_t *task, config_t *config);

#endif // GEN_H
