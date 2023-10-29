#include "multi.h"

#include "brute.h"
#include "common.h"
#include "queue.h"
#include "single.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

status_t
mt_context_init (mt_context_t *context, config_t *config)
{
  if (pthread_mutex_init (&context->mutex, NULL) != 0)
    {
      print_error ("Could not initialize a mutex\n");
      return (S_FAILURE);
    }

  if (pthread_cond_init (&context->cond_sem, NULL) != 0)
    {
      print_error ("Could not initialize a condition variable\n");
      return (S_FAILURE);
    }

  if (queue_init (&context->queue) == S_FAILURE)
    {
      print_error ("Could not initialize a queue\n");
      return (S_FAILURE);
    }

  if (thread_pool_init (&context->thread_pool) == S_FAILURE)
    return (S_FAILURE);

  context->config = config;
  context->passwords_remaining = 0;
  context->password[0] = 0;

  return (S_SUCCESS);
}

status_t
mt_context_destroy (mt_context_t *context)
{
  if (thread_pool_cancel (&context->thread_pool) == S_FAILURE)
    {
      print_error ("Could not cancel a thread pool\n");
      return (S_FAILURE);
    }

  if (queue_destroy (&context->queue) == S_FAILURE)
    {
      print_error ("Could not destroy a queue\n");
      return (S_FAILURE);
    }

  if (pthread_cond_destroy (&context->cond_sem) != 0)
    {
      print_error ("Could not destroy a condition variable\n");
      return (S_FAILURE);
    }

  if (pthread_mutex_destroy (&context->mutex) != 0)
    {
      print_error ("Could not destroy a mutex\n");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

void *
mt_password_check (void *context)
{
  mt_context_t *mt_ctx = (mt_context_t *)context;
  task_t task;
  st_context_t st_ctx = {
    .hash = mt_ctx->config->hash,
    .data = { .initialized = 0 },
  };

  while (true)
    {
      if (queue_pop (&mt_ctx->queue, &task) == S_FAILURE)
        return (NULL);

      task.to = task.from;
      task.from = 0;

      if (brute (&task, mt_ctx->config, st_password_check, &st_ctx))
        memcpy (mt_ctx->password, task.password, sizeof (task.password));

      if (pthread_mutex_lock (&mt_ctx->mutex) != 0)
        {
          print_error ("Could not lock a mutex\n");
          return (NULL);
        }
      pthread_cleanup_push (cleanup_mutex_unlock, &mt_ctx->mutex);

      --mt_ctx->passwords_remaining;
      if (mt_ctx->passwords_remaining == 0 || mt_ctx->password[0] != 0)
        if (pthread_cond_signal (&mt_ctx->cond_sem) != 0)
          {
            print_error ("Could not signal a condition\n");
            return (NULL);
          }

      pthread_cleanup_pop (!0);
    }
  return (NULL);
}

bool
queue_push_wrapper (task_t *task, void *context)
{
  mt_context_t *mt_ctx = (mt_context_t *)context;

  if (pthread_mutex_lock (&mt_ctx->mutex) != 0)
    {
      print_error ("Could not lock a mutex\n");
      return (false);
    }

  ++mt_ctx->passwords_remaining;

  if (pthread_mutex_unlock (&mt_ctx->mutex) != 0)
    {
      print_error ("Could not unlock a mutex\n");
      return (false);
    }

  if (queue_push (&mt_ctx->queue, task) == S_FAILURE)
    {
      print_error ("Could not push to a queue\n");
      return (false);
    }

  return (mt_ctx->password[0] != 0);
}

bool
run_multi (task_t *task, config_t *config)
{
  mt_context_t context;

  if (mt_context_init (&context, config) == S_FAILURE)
    return (false);

  int active_threads = 0;
  for (int i = 0; i < config->number_of_threads; ++i)
    {
      if (thread_create (&context.thread_pool, mt_password_check, &context)
          == S_SUCCESS)
        ++active_threads;
    }

  if (active_threads == 0)
    {
      print_error ("Could not create a single thread\n");
      return (false);
    }

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  brute (task, config, queue_push_wrapper, &context);

  if (pthread_mutex_lock (&context.mutex) != 0)
    {
      print_error ("Could not lock a mutex\n");
      return (false);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &context.mutex);

  while (context.passwords_remaining != 0 && context.password[0] == 0)
    if (pthread_cond_wait (&context.cond_sem, &context.mutex) != 0)
      {
        print_error ("Could not wait on a condition\n");
        return (false);
      }

  pthread_cleanup_pop (!0);

  if (queue_cancel (&context.queue) == S_FAILURE)
    {
      print_error ("Could not cancel a queue\n");
      return (false);
    }

  if (context.password[0] != 0)
    memcpy (task->password, context.password, sizeof (context.password));

  if (mt_context_destroy (&context) == S_FAILURE)
    return (false);

  return (context.password[0] != 0);
}
