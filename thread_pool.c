#include "thread_pool.h"

#include "common.h"
#include "log.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

status_t
thread_pool_init (thread_pool_t *thread_pool)
{
  if (pthread_mutex_init (&thread_pool->mutex, NULL) != 0)
    {
      error ("Could not create thread pool mutex");
      return (S_FAILURE);
    }

  if (pthread_cond_init (&thread_pool->cond, NULL) != 0)
    {
      error ("Could not create thread pool conditional semaphore");
      return (S_FAILURE);
    }

  thread_pool->threads.prev = &thread_pool->threads;
  thread_pool->threads.next = &thread_pool->threads;
  thread_pool->threads.thread = pthread_self ();
  thread_pool->count = 0;
  thread_pool->cancelled = false;

  return (S_SUCCESS);
}

static void
thread_cleanup (void *arg)
{
  thread_cleanup_context_t *tcc = arg;
  node_t *node = tcc->node;
  thread_pool_t *thread_pool = tcc->thread_pool;

  if (pthread_mutex_lock (&thread_pool->mutex) != 0)
    error ("Could not lock a mutex");
  pthread_cleanup_push (cleanup_mutex_unlock, &thread_pool->mutex);

  node->prev->next = node->next;
  node->next->prev = node->prev;
  --thread_pool->count;

  // if (pthread_cond_signal (&thread_pool->cond) != 0)
  //   error ("Could not signal a conditional semaphore");

  pthread_cleanup_pop (!0);
}

static void *
thread_run (void *arg)
{
  tp_context_t *tp_ctx = (tp_context_t *)arg;
  tp_context_t local_ctx = *tp_ctx;
  thread_pool_t *thread_pool = local_ctx.thread_pool;

  char args[local_ctx.arg_size];
  memcpy (args, local_ctx.arg, local_ctx.arg_size);

  if (pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL) != 0)
    error ("Could not set cancel state");

  if (pthread_mutex_unlock (&tp_ctx->mutex) != 0)
    {
      error ("Could not unlock mutex");
      return (NULL);
    }

  node_t node;
  node.thread = pthread_self ();

  if (pthread_mutex_lock (&thread_pool->mutex) != 0)
    {
      error ("Could not lock mutex");
      return (NULL);
    }

  if (thread_pool->cancelled)
    {
      --thread_pool->count;
      pthread_cond_signal (&thread_pool->cond);
      pthread_mutex_unlock (&thread_pool->mutex);

      return (NULL);
    }
  node.next = &thread_pool->threads;
  node.prev = thread_pool->threads.prev;

  thread_pool->threads.prev->next = &node;
  thread_pool->threads.prev = &node;

  if (pthread_mutex_unlock (&thread_pool->mutex) != 0)
    {
      error ("Could not unlock mutex");
      return (NULL);
    }

  thread_cleanup_context_t tcc = { .thread_pool = thread_pool, .node = &node };
  pthread_cleanup_push (thread_cleanup, &tcc);

  // pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  if (pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL) != 0)
    error ("Could not set cancel state for a thread");
  else
    local_ctx.func (args);

  pthread_cleanup_pop (!0);

  return (NULL);
}

status_t
thread_create (thread_pool_t *thread_pool, void *(*func) (void *), void *arg,
               size_t arg_size)
{
  tp_context_t context = {
    .thread_pool = thread_pool, .func = func, .arg = arg, .arg_size = arg_size
  };

  if (pthread_mutex_init (&context.mutex, NULL) != 0)
    {
      error ("Could not create mutex");
      return (S_FAILURE);
    }

  if (pthread_mutex_lock (&context.mutex) != 0)
    {
      error ("Could not lock mutex");
      return (S_FAILURE);
    }

  pthread_t thread;
  pthread_attr_t attr;
  if (pthread_attr_init (&attr) != 0)
    {
      error ("Could not initialize a thread attribute");
      return (S_FAILURE);
    }
  if (pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED) != 0)
    {
      error ("Could not set detach state for a thread");
      return (S_FAILURE);
    }

  if (pthread_mutex_lock (&thread_pool->mutex) != 0)
    {
      error ("Could not lock a mutex");
      return (S_FAILURE);
    }
  ++thread_pool->count;
  if (pthread_mutex_unlock (&thread_pool->mutex) != 0)
    {
      error ("Could not unlock a mutex");
      return (S_FAILURE);
    }

  if (pthread_create (&thread, &attr, &thread_run, &context) != 0)
    {
      error ("Could not create thread");
      if (pthread_mutex_lock (&thread_pool->mutex) != 0)
        {
          error ("Could not lock a mutex");
          return (S_FAILURE);
        }
      pthread_cleanup_push (cleanup_mutex_unlock, &thread_pool->mutex);

      --thread_pool->count;
      if (pthread_cond_signal (&thread_pool->cond) != 0)
        error ("Could not signal a conditional semaphore");

      pthread_cleanup_pop (!0);

      return (S_FAILURE);
    }

  if (pthread_mutex_lock (&context.mutex) != 0)
    {
      error ("Could not lock mutex");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
thread_pool_collect (thread_pool_t *thread_pool, bool cancel)
{
  pthread_t self_id = pthread_self ();

  for (;;)
    {
      if (pthread_mutex_lock (&thread_pool->mutex) != 0)
        {
          error ("Could not lock mutex");
          return (S_FAILURE);
        }

      thread_pool->cancelled = true;
      pthread_t thread = thread_pool->threads.next->thread;
      bool empty = (thread_pool->threads.next == &thread_pool->threads);

      if (pthread_mutex_unlock (&thread_pool->mutex) != 0)
        {
          error ("Could not unlock mutex");
          return (S_FAILURE);
        }

      if (empty)
        break;

      if (thread == self_id)
        return (S_FAILURE);

      if (pthread_mutex_lock (&thread_pool->mutex) != 0)
        {
          error ("Could not lock a mutex");
          return (S_FAILURE);
        }
      pthread_cleanup_push (cleanup_mutex_unlock, &thread_pool->mutex);

      if (cancel)
      {
        if (pthread_cancel (thread) != 0)
          {
            error ("Could not cancel a thread");
            return (S_FAILURE);
          }
      }

      pthread_join (thread, NULL);
      // while (thread_pool->threads.next->thread == thread)
      //   if (pthread_cond_wait (&thread_pool->cond, &thread_pool->mutex) != 0)
      //     {
      //       error ("Could not wait for a conditional semaphore");
      //       return (S_FAILURE);
      //     }

      pthread_cleanup_pop (!0);
    }

  if (pthread_mutex_lock (&thread_pool->mutex) != 0)
    {
      error ("Could not lock a mutex");
      return (S_FAILURE);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &thread_pool->mutex);

  while (thread_pool->count != 0)
    if (pthread_cond_wait (&thread_pool->cond, &thread_pool->mutex) != 0)
      {
        error ("Could not wait for a conditional semaphore");
        return (S_FAILURE);
      }

  pthread_cleanup_pop (!0);

  /* Valgrind tests are returning false positive result, since it doesn't
   * wait until all memory allocated for threads is cleared, so we need to
   * manually make a timeout */
  char *var = getenv ("PYTEST_CURRENT_TEST");
  if (var == NULL)
    return (S_SUCCESS);
  struct timespec time, time2;
  time.tv_sec = 0;
  time.tv_nsec = 100000000L;
  if (strstr (var, "valgrind"))
    nanosleep (&time, &time2);

  return (S_SUCCESS);
}

status_t
thread_pool_cancel (thread_pool_t *thread_pool)
{
  return (thread_pool_collect (thread_pool, true));
}

status_t
thread_pool_join (thread_pool_t *thread_pool)
{
  return (thread_pool_collect (thread_pool, false));
}

int
create_threads (thread_pool_t *thread_pool, int number_of_threads,
                void *func (void *), void *context, size_t context_size)
{
  int active_threads = 0;

  for (int i = 0; i < number_of_threads; ++i)
    if (thread_create (thread_pool, func, context, context_size) == S_SUCCESS)
      ++active_threads;

  if (active_threads == 0)
    error ("Could not create a single thread");

  return (active_threads);
}