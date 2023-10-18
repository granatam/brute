#ifndef REC_H
#define REC_H

#include "common.h"
#include "config.h"
#include <ucontext.h>
#include <signal.h>

#define STACK_SIZE MINSIGSTKSZ

typedef struct rec_state_t
{
  ucontext_t contexts[2];
  char stack[STACK_SIZE];
  task_t *task;
  bool cancelled;
} rec_state_t;

void rec_state_init (rec_state_t *state, task_t *task, config_t *config);
bool rec_state_next (rec_state_t *state);

bool brute_rec (task_t *task, config_t *config,
                password_handler_t password_handler, void *context, int pos);
bool brute_rec_wrapper (task_t *task, config_t *config,
                        password_handler_t password_handler, void *context);

#endif // REC_H
