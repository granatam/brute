#include "gen.h"

bool
run_generator (task_t *task, config_t *config)
{
  gen_context_t context;

  if (pthread_mutex_init (&context.mutex, NULL) != 0)
    {
      print_error ("Could not initialize a mutex\n");
      return (false);
    }

  iter_state_init (&context.state, config->alph, task);
  
  context.cancelled = false;

  // Remove it later
  return false;
}
