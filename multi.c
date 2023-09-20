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
      if (queue_pop (&mt_context->queue, &task) == S_FAILURE)
        {
          return NULL;
        }
      if (st_password_check (&task, &st_context))
        {
          memcpy (mt_context->password, task.password, sizeof (task.password));
        }

      if (pthread_mutex_lock (&mt_context->mutex) != 0)
        {
          return NULL;
        }
      pthread_cleanup_push (cleanup_mutex_unlock, &mt_context->mutex);
      --mt_context->passwords_remaining;
      if (mt_context->passwords_remaining == 0 || mt_context->password[0] != 0)
        {
          if (pthread_cond_signal (&mt_context->cond_sem) != 0)
            {
              return NULL;
            }
        }
      pthread_cleanup_pop (!0);
    }
  return NULL;
}

bool
queue_push_wrapper (task_t *task, void *context)
{
  mt_context_t *mt_context = (mt_context_t *)context;
  if (pthread_mutex_lock (&mt_context->mutex) != 0)
    {
      return false;
    }
  ++mt_context->passwords_remaining;
  if (pthread_mutex_unlock (&mt_context->mutex) != 0)
    {
      return false;
    }

  if (queue_push (&mt_context->queue, task) == S_FAILURE)
    {
      return false;
    }

  return mt_context->password[0] != 0;
}

// TODO: Change functions return type to status_t to check for errors? Also
// deal with `return NULL;` in `mt_password_check ()` because its not possible
// now to check for errors
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
  int active_threads = 0;
  for (int i = 0; i < number_of_cpus; ++i)
    {
      if (pthread_create (&threads[i], NULL, mt_password_check,
                          (void *)&context)
          == 0)
        {
          ++active_threads;
        }
    }

  if (active_threads == 0)
    {
      print_error ("Error: 0 active threads\n");
      return false;
    }
  // TODO: I think I need to do some checks with this function but didn't
  // realized what checks yet
  brute (task, config, queue_push_wrapper, &context);

  if (pthread_mutex_lock (&context.mutex) != 0)
    {
      return false;
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &context.mutex);
  while (context.passwords_remaining != 0 && context.password[0] == 0)
    {
      if (pthread_cond_wait (&context.cond_sem, &context.mutex) != 0)
        {
          return false;
        }
    }
  pthread_cleanup_pop (!0);

  queue_cancel (&context.queue);

  for (int i = 0; i < active_threads; ++i)
    {
      pthread_join (threads[i], NULL);
    }

  if (context.password[0] != 0)
    {
      memcpy (task->password, context.password, sizeof (context.password));
    }

  return context.password[0] != 0;
}
