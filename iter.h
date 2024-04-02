#ifndef ITER_H
#define ITER_H

#include "common.h"
#include "config.h"

typedef struct iter_state_t
{
  base_state_t base_state;
  char *alph;
  int alph_size;
  int idx[MAX_PASSWORD_LENGTH];
} iter_state_t;

void iter_state_init (iter_state_t *state, char *alph, task_t *task);
bool iter_state_next (iter_state_t *state);

bool brute_iter (task_t *task, char *alph,
                 password_handler_t password_handler, void *context);

#endif // ITER_H
