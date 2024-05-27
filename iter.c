#include "iter.h"

#include "log.h"

#include <string.h>

void
iter_state_init (iter_state_t *state, char *alph, task_t *task)
{
  state->alph = alph;
  state->alph_size = strlen (alph) - 1;
  state->base_state.task = task;

  // trace ("%p %d %d", task, task->to, state->base_state.task->to);
  memset (state->idx, 0, state->base_state.task->to * sizeof (state->idx[0]));
  memset (&state->base_state.task->task.password[state->base_state.task->from],
          alph[0], state->base_state.task->to - state->base_state.task->from);
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
      task->task.password[pos] = state->alph[state->idx[pos]];
    }

  if (pos < task->from)
    return (false);

  task->task.password[pos] = state->alph[++state->idx[pos]];

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
