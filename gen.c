#include "gen.h"

#include "brute.h"
#include "single.h"
#include <string.h>

status_t
gen_context_init (gen_context_t *context, config_t *config, task_t *task)
{
  if (pthread_mutex_init (&context->mutex, NULL) != 0)
    {
      print_error ("Could not initialize a mutex\n");
      return (S_FAILURE);
    }

  task->from = 0;
  task->to = config->length;

  iter_state_init (&context->state, config->alph, task);

  context->config = config;
  context->cancelled = false;
  context->password[0] = 0;

  return (S_SUCCESS);
}

void *
gen_worker (void *context)
{
  gen_context_t *gen_context = (gen_context_t *)context;

  st_context_t st_context = {
    .hash = gen_context->config->hash,
    .data = { .initialized = 0 },
  };

  while (!gen_context->cancelled && gen_context->password[0] == 0)
    {
      if (pthread_mutex_lock (&gen_context->mutex) != 0)
        {
          print_error ("Could not lock a mutex\n");
          return (NULL);
        }

      task_t current_task = *gen_context->state.task;

      if (!gen_context->cancelled && gen_context->password[0] == 0)
        gen_context->cancelled = !iter_state_next (&gen_context->state);

      if (pthread_mutex_unlock (&gen_context->mutex) != 0)
        {
          print_error ("Could not unlock a mutex\n");
        }

      if (gen_context->cancelled || gen_context->password[0] != 0)
        break;

      if (st_password_check (&current_task, &st_context))
        {
          memcpy (gen_context->password, current_task.password,
                  sizeof (current_task.password));
          gen_context->cancelled = true;
        }
    }
  return (NULL);
}

bool
run_generator (task_t *task, config_t *config)
{
  gen_context_t context;

  if (gen_context_init (&context, config, task) == S_FAILURE)
    return (false);

  int number_of_threads
      = (config->number_of_threads == 1) ? 1 : config->number_of_threads - 1;
  pthread_t threads[number_of_threads];

  int active_threads
      = create_threads (threads, number_of_threads, gen_worker, &context);
  if (active_threads == 0)
    return (false);

  gen_worker (&context);

  for (int i = 0; i < active_threads; ++i)
    pthread_join (threads[i], NULL);

  if (pthread_mutex_destroy (&context.mutex) != 0)
    {
      print_error ("Could not destroy a mutex\n");
      return (false);
    }

  if (context.password[0] != 0)
    memcpy (task->password, context.password, sizeof (context.password));

  return (context.password[0] != 0);
}
