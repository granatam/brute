#ifndef GEN_H
#define GEN_H

#include "common.h"
#include "config.h"
#include "iter.h"
#include <pthread.h>

typedef struct gen_context_t
{
  base_state_t *state;
  bool (*state_next) (void *state);
  pthread_mutex_t mutex;
  config_t *config;
  password_t password;
  bool cancelled;
} gen_context_t;

bool run_generator (task_t *task, config_t *config);

#endif // GEN_H
