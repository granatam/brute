#include "gen.h"

#include "brute.h"
#include "common.h"
#include "iter.h"
#include "rec.h"
#include "single.h"

#include <stdlib.h>
#include <string.h>

static status_t
gen_context_init (gen_context_t *context, config_t *config, task_t *task)
{
  if (pthread_mutex_init (&context->mutex, NULL) != 0)
    {
      print_error ("Could not initialize a mutex\n");
      return (S_FAILURE);
    }

  task->from = 0;
  task->to = config->length;

  switch (config->brute_mode)
    {
    case BM_RECU:
    case BM_REC_GEN:
#ifndef __APPLE__
      if (!(context->state = malloc (sizeof (rec_state_t))))
        goto malloc_fail;
      rec_state_init ((rec_state_t *)context->state, task, config);
      context->state_next = (bool (*) (base_state_t *))rec_state_next;
      break;
#endif
    case BM_ITER:
      if (!(context->state = malloc (sizeof (iter_state_t))))
        goto malloc_fail;
      iter_state_init ((iter_state_t *)context->state, config->alph, task);
      context->state_next = (bool (*) (base_state_t *))iter_state_next;
      break;
    }

  context->config = config;
  context->cancelled = false;
  context->password[0] = 0;

  return (S_SUCCESS);

malloc_fail:
  print_error ("Could not allocate memory for context state\n");
  return (S_FAILURE);
}

static status_t
gen_context_destroy (gen_context_t *context)
{
  context->cancelled = true;

  if (pthread_mutex_destroy (&context->mutex) != 0)
    {
      print_error ("Could not destroy a mutex\n");
      return (S_FAILURE);
    }

  if (context->state)
    free (context->state);

  return (S_SUCCESS);
}

static void *
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

      task_t current_task = *gen_context->state->task;

      if (!gen_context->cancelled && gen_context->password[0] == 0)
        gen_context->cancelled = !gen_context->state_next (gen_context->state);

      if (pthread_mutex_unlock (&gen_context->mutex) != 0)
        {
          print_error ("Could not unlock a mutex\n");
        }

      if (gen_context->cancelled || gen_context->password[0] != 0)
        break;

      current_task.to = current_task.from;
      current_task.from = 0;
      if (brute (&current_task, gen_context->config, st_password_check,
                 &st_context))
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

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;
  if (gen_context_init (&context, config, task) == S_FAILURE)
    {
      gen_context_destroy (&context);
      return (false);
    }

  int number_of_threads
      = (config->number_of_threads == 1) ? 1 : config->number_of_threads - 1;
  pthread_t threads[number_of_threads];

  int active_threads
      = create_threads (threads, number_of_threads, gen_worker, &context);
  if (active_threads == 0)
    goto fail;

  gen_worker (&context);

  for (int i = 0; i < active_threads; ++i)
    pthread_join (threads[i], NULL);

  if (pthread_mutex_destroy (&context.mutex) != 0)
    {
      print_error ("Could not destroy a mutex\n");
      goto fail;
    }

  if (context.password[0] != 0)
    memcpy (task->password, context.password, sizeof (context.password));

  gen_context_destroy (&context);

  return (context.password[0] != 0);

fail:
  gen_context_destroy (&context);
  return (false);
}
