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

  iter_state_init (&context->state, config->alph, task);

  context->config = config;
  context->cancelled = false;
  context->password[0] = 0;

  return (S_SUCCESS);
}

void *
generator (void *context)
{
  gen_context_t *gen_context = (gen_context_t *)context;
  gen_context->state.task->from = 0;
  gen_context->state.task->to = gen_context->config->length;

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
      pthread_cleanup_push (cleanup_mutex_unlock, &gen_context->mutex);

      gen_context->cancelled = !iter_state_next (&gen_context->state);

      pthread_cleanup_pop (!0);

      gen_context->state.task->to = gen_context->state.task->from;
      gen_context->state.task->from = 0;
      if (brute (gen_context->state.task, gen_context->config,
                 st_password_check, &st_context))
        {
          memcpy (gen_context->password, gen_context->state.task->password,
                  sizeof (gen_context->state.task->password));
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

  context.state.task->from = 2;
  context.state.task->to = config->length;

  pthread_t threads[config->number_of_threads];
  int active_threads = create_threads (threads, config->number_of_threads,
                                       generator, &context);
  if (active_threads == 0)
    return (false);

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
