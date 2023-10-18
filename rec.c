#include "rec.h"

bool
brute_rec_gen_handler (task_t *task, void *context)
{
  (void) task;
  rec_state_t *state = (rec_state_t *)context;
  swapcontext (&state->contexts[1], &state->contexts[0]);
  return false;
}

void 
brute_rec_gen_helper (config_t *config, rec_state_t *state)
{
  state->cancelled = false;
  brute_rec_wrapper (state->task, config, brute_rec_gen_handler, state); 
  state->cancelled = true;
}

void
rec_state_init (rec_state_t *state, task_t *task, config_t *config)
{
  state->task = task;

  getcontext (&state->contexts[0]);
  state->contexts[1] = state->contexts[0];
  state->contexts[1].uc_stack.ss_sp = state->stack;
  state->contexts[1].uc_stack.ss_size = sizeof (state->stack);
  state->contexts[1].uc_link = &state->contexts[0];
  makecontext (&state->contexts[1], (void (*) (void)) brute_rec_gen_helper, 2, config, state);

  swapcontext (&state->contexts[0], &state->contexts[1]);
}

bool
rec_state_next (rec_state_t *state)
{
  swapcontext(&state->contexts[0], &state->contexts[1]);
  return (!state->cancelled);
}

bool
brute_rec_gen (task_t *task, config_t *config, password_handler_t password_handler,
	       void *context)
{
  rec_state_t state;
  rec_state_init (&state, task, config);
  while (true) 
  {
    if (password_handler (context, task))
      return (true);
    if (!rec_state_next (&state))
      return (false);
  }
}

bool
brute_rec (task_t *task, config_t *config, password_handler_t password_handler,
           void *context, int pos)
{
  if (pos == task->to)
    return (password_handler (task, context));
  else
    {
      for (size_t i = 0; config->alph[i] != '\0'; ++i)
        {
          task->password[pos] = config->alph[i];

          if (brute_rec (task, config, password_handler, context, pos + 1))
            return (true);
        }
    }
  return (false);
}

bool
brute_rec_wrapper (task_t *task, config_t *config,
                   password_handler_t password_handler, void *context)
{
  return (brute_rec (task, config, password_handler, context, task->from));
}

