#ifndef REC_H
#define REC_H

#include "common.h"
#include "config.h"

#ifndef __APPLE__
#include <signal.h>
#include <ucontext.h>

#define STACK_SIZE MINSIGSTKSZ

typedef struct rec_state_t
{
  base_state_t base_state;
  ucontext_t main_context;
  ucontext_t rec_context;
  char stack[STACK_SIZE];
  bool cancelled;
} rec_state_t;

void rec_state_init (rec_state_t *state, task_t *task, char *alph);
bool rec_state_next (rec_state_t *state);

bool brute_rec_gen (task_t *task, char *alph,
                    password_handler_t password_handler, void *context);
#endif // ifndef __APPLE__

bool brute_rec_wrapper (task_t *task, char *alph,
                        password_handler_t password_handler, void *context);

#endif // REC_H
