#include "rec.h"

#ifndef __APPLE__
static bool
brute_rec_gen_handler (task_t *task, void *context)
{
  (void)task; /* to suppress "unused parameter" warning */
  rec_state_t *state = (rec_state_t *)context;

  if (swapcontext (&state->rec_context, &state->main_context) != 0)
    {
      print_error ("Could not swap contexts\n");
      state->cancelled = true;
    }

  return (false);
}

static void
brute_rec_gen_helper (char *alph, rec_state_t *state)
{
  brute_rec_wrapper (state->base_state.task, alph, brute_rec_gen_handler,
                     state);
  state->cancelled = true;
}

void
rec_state_init (rec_state_t *state, task_t *task, char *alph)
{
  state->base_state.task = task;
  state->cancelled = false;

  if (getcontext (&state->main_context) != 0)
    {
      print_error ("Could not get context\n");
      state->cancelled = true;
      return;
    }
  state->rec_context = state->main_context;
  state->rec_context.uc_stack.ss_sp = &state->stack;
  state->rec_context.uc_stack.ss_size = sizeof (state->stack);
  state->rec_context.uc_link = &state->main_context;

  makecontext (&state->rec_context, (void (*) (void))brute_rec_gen_helper, 2,
               alph, state);

  if (swapcontext (&state->main_context, &state->rec_context) != 0)
    {
      print_error ("Could not swap contexts\n");
      state->cancelled = true;
    }
}

bool
rec_state_next (rec_state_t *state)
{
  if (!state->cancelled)
    if (swapcontext (&state->main_context, &state->rec_context) != 0)
      {
        print_error ("Could not swap contexts\n");
        return (false);
      }
  return (!state->cancelled);
}

bool
brute_rec_gen (task_t *task, char *alph, password_handler_t password_handler,
               void *context)
{
  rec_state_t state;
  rec_state_init (&state, task, alph);
  while (true)
    {
      if (password_handler (state.base_state.task, context))
        return (true);
      if (!rec_state_next (&state))
        return (false);
    }
}
#endif // ifndef __APPLE__

static bool
brute_rec (task_t *task, char *alph, password_handler_t password_handler,
           void *context, int pos)
{
  if (pos == task->to)
    {
      // print_error ("%s\n", task->password);
      return (password_handler (task, context));
    }
  else
    {
      for (size_t i = 0; alph[i] != '\0'; ++i)
        {
          task->password[pos] = alph[i];

          if (brute_rec (task, alph, password_handler, context, pos + 1))
            return (true);
        }
    }
  return (false);
}

bool
brute_rec_wrapper (task_t *task, char *alph,
                   password_handler_t password_handler, void *context)
{
  return (brute_rec (task, alph, password_handler, context, task->from));
}
