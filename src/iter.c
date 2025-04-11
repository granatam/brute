#include "iter.h"

#include <string.h>

void
iter_state_init (iter_state_t *state, char *alph, task_t *task)
{
  state->alph = alph;
  state->alph_size = strlen (alph) - 1;

  memset (state->idx, 0, task->to * sizeof (state->idx[0]));
  memset (&task->result.password[task->from], alph[0], task->to - task->from);

  state->base_state.task = task;
}

bool
iter_state_next (iter_state_t *state)
{
  task_t *task = state->base_state.task;

  int pos;
  for (pos = task->to - 1;
       pos >= task->from && state->idx[pos] == state->alph_size; --pos)
    {
      state->idx[pos] = 0;
      task->result.password[pos] = state->alph[state->idx[pos]];
    }

  if (pos < task->from)
    return (false);

  task->result.password[pos] = state->alph[++state->idx[pos]];

  return (true);
}

bool
brute_iter (task_t *task, char *alph, password_handler_t password_handler,
            void *context)
{
  iter_state_t state;
  iter_state_init (&state, alph, task);

  while (true)
    {
      if (password_handler (state.base_state.task, context))
        return (true);

      if (!iter_state_next (&state))
        return (false);
    }
}
