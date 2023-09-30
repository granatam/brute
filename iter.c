#include "iter.h"

#include <string.h>

// TODO: Change return type to `status_t` and return error if state/config/task
// is null?
void
iter_state_init (iter_state_t *state, char *alph, task_t *task)
{
  state->alph = alph;
  state->alph_size = strlen (alph) - 1;
  state->task = task;

  memset (state->idx, 0, state->task->to * sizeof (int));
  memset (state->task->password, alph[0], state->task->to);
}

bool
iter_state_next (iter_state_t *state)
{
  int pos;
  for (pos = state->task->to - 1;
       pos >= state->task->from && state->idx[pos] == state->alph_size; --pos)
    {
      state->idx[pos] = 0;
      state->task->password[pos] = state->alph[state->idx[pos]];
    }

  if (pos < state->task->from)
    return (false);

  state->task->password[pos] = state->alph[++state->idx[pos]];

  return (true);
}

bool
brute_iter (task_t *task, config_t *config,
            password_handler_t password_handler, void *context)
{
  iter_state_t state;
  iter_state_init (&state, config->alph, task);

  while (true)
    {
      if (password_handler (state.task, context))
        return (true);

      if (!iter_state_next (&state))
        return (false);
    }
}
