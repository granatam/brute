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
      if (!(context->state = calloc (1, sizeof (rec_state_t))))
        goto alloc_fail;
      rec_state_init ((rec_state_t *)context->state, task, config->alph);
      context->state_next = (bool (*) (base_state_t *))rec_state_next;
      break;
#endif
    case BM_ITER:
      if (!(context->state = calloc (1, sizeof (iter_state_t))))
        goto alloc_fail;
      iter_state_init ((iter_state_t *)context->state, config->alph, task);
      context->state_next = (bool (*) (base_state_t *))iter_state_next;
      break;
    }

  if (thread_pool_init (&context->thread_pool) == S_FAILURE)
    {
      print_error ("Could not initialize a thread pool\n");
      return (S_FAILURE);
    }

  context->config = config;
  context->cancelled = false;
  context->password[0] = 0;

  return (S_SUCCESS);

alloc_fail:
  print_error ("Could not allocate memory for context state\n");
  return (S_FAILURE);
}

static status_t
gen_context_destroy (gen_context_t *context)
{
  context->cancelled = true;

  if (thread_pool_join (&context->thread_pool) == S_FAILURE)
    {
      print_error ("Could not cancel a thread pool\n");
      return (S_FAILURE);
    }

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
  gen_context_t *gen_ctx = *(gen_context_t **)context;

  st_context_t st_ctx = {
    .hash = gen_ctx->config->hash,
    .data = { .initialized = 0 },
  };

  while (!gen_ctx->cancelled && gen_ctx->password[0] == 0)
    {
      if (pthread_mutex_lock (&gen_ctx->mutex) != 0)
        {
          print_error ("Could not lock a mutex\n");
          return (NULL);
        }

      task_t task = *gen_ctx->state->task;

      if (!gen_ctx->cancelled && gen_ctx->password[0] == 0)
        gen_ctx->cancelled = !gen_ctx->state_next (gen_ctx->state);

      if (pthread_mutex_unlock (&gen_ctx->mutex) != 0)
        {
          print_error ("Could not unlock a mutex\n");
          return (NULL);
        }

      if (gen_ctx->cancelled || gen_ctx->password[0] != 0)
        break;

      task.to = task.from;
      task.from = 0;
      if (brute (&task, gen_ctx->config, st_password_check, &st_ctx))
        {
          memcpy (gen_ctx->password, task.task.password,
                  sizeof (task.task.password));
          gen_ctx->cancelled = true;
        }
    }
  return (NULL);
}

bool
run_generator (task_t *task, config_t *config)
{
  gen_context_t context;
  gen_context_t *context_ptr = &context;

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;
  if (gen_context_init (&context, config, task) == S_FAILURE)
    goto fail;

  int number_of_threads
      = (config->number_of_threads == 1) ? 1 : config->number_of_threads - 1;
  int active_threads
      = create_threads (&context.thread_pool, number_of_threads, gen_worker,
                        &context_ptr, sizeof (context_ptr));

  if (active_threads == 0)
    goto fail;

  gen_worker (&context_ptr);

  if (gen_context_destroy (&context) == S_FAILURE)
    {
      print_error ("Could not destroy generator context\n");
      return (false);
    }

  if (context.password[0] != 0)
    memcpy (task->task.password, context.password, sizeof (context.password));

  return (context.password[0] != 0);

fail:
  if (gen_context_destroy (&context) == S_FAILURE)
    print_error ("Could not return generator context\n");

  return (false);
}
