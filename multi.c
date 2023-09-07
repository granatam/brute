#include "multi.h"

#include "brute.h"
#include "queue.h"
#include "single.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void *
mt_password_check (void *context)
{
  mt_context_t *mt_context = (mt_context_t *)context;
  task_t task;
  st_context_t st_context = {
    .hash = mt_context->config->hash,
    .data = { .initialized = 0 },
  };

  while (true)
    {
      /* don't understand why we need status check here */
      if (queue_pop (&mt_context->queue, &task) == S_FAILURE)
        {
          return NULL;
        }
      if (st_password_check (&task, &st_context))
        {
          /* This is printing but loop doesn't break */
          /* printf ("Password found: %s\n", task.password); */
          memcpy (mt_context->password, task.password, sizeof (task.password));
        }

      if (pthread_mutex_lock (&mt_context->mutex) != 0)
        {
          return NULL;
        }
      --mt_context->passwords_remaining;
      /* printf("-- %d\n", mt_context->passwords_remaining); */
      if (mt_context->passwords_remaining == 0)
        {
          if (pthread_cond_signal (&mt_context->cond_sem) != 0)
            {
              return NULL;
            }
        }
      if (pthread_mutex_unlock (&mt_context->mutex) != 0)
        {
          return NULL;
        }
    }
  return NULL;
}

bool
queue_push_wrapper (task_t *task, void *context)
{
  mt_context_t *mt_context = (mt_context_t *)context;
  if (queue_push (&mt_context->queue, task) == S_FAILURE)
    {
      return false;
    }

  if (pthread_mutex_lock (&mt_context->mutex) != 0)
    {
      return false;
    }
  ++mt_context->passwords_remaining;
  if (pthread_mutex_unlock (&mt_context->mutex) != 0)
    {
      return false;
    }
  /* printf("++ %d\n", mt_context->passwords_remaining); */

  return mt_context->password[0] != 0;
}

// TODO: Find memory leak and fix it
// TODO: Change functions return type to status_t to check for errors? Also deal
// with `return NULL;` in `mt_password_check ()` because its not possible now to
// check for errors
bool
run_multi (task_t *task, config_t *config)
{
  mt_context_t context;
  if (pthread_mutex_init (&context.mutex, NULL) != 0)
    {
      return false;
    }
  if (pthread_cond_init (&context.cond_sem, NULL) != 0)
    {
      return false;
    }
  queue_init (&context.queue);
  context.config = config;
  context.passwords_remaining = 0;
  context.password[0] = 0;

  int number_of_cpus = sysconf (_SC_NPROCESSORS_ONLN);
  pthread_t threads[number_of_cpus];
  for (int i = 0; i < number_of_cpus; ++i)
    {
      pthread_create (&threads[i], NULL, mt_password_check, (void *)&context);
    }

  // TODO: Get rid of is_found
  bool is_found = false;
  switch (config->brute_mode)
    {
    case BM_ITER:
      is_found = brute_iter (task, config, queue_push_wrapper, &context);
      break;
    case BM_RECU:
      is_found
          = brute_rec_wrapper (task, config, queue_push_wrapper, &context);
      break;
    }

  queue_cancel (&context.queue);

  if (pthread_mutex_lock (&context.mutex) != 0)
    {
      return false;
    }
  while (context.passwords_remaining != 0)
    {
      if (pthread_cond_wait (&context.cond_sem, &context.mutex) != 0)
        {
          return false;
        }
    }
  if (pthread_mutex_unlock (&context.mutex) != 0)
    {
      return false;
    }

  for (int i = 0; i < number_of_cpus; ++i)
    {
      pthread_join (threads[i], NULL);
    }

  return is_found;
}
