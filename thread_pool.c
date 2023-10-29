#include "thread_pool.h"
#include "common.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

// TODO: Add status checks and cleanup in case of errors
// TODO: Check if code could be more readable

status_t
thread_pool_init (thread_pool_t *thread_pool)
{
  if (pthread_mutex_init (&thread_pool->mutex, NULL) != 0)
    {
      print_error ("Could not create thread pool mutex\n");
      return (S_FAILURE);
    }

  if (pthread_cond_init (&thread_pool->cond, NULL) != 0)
    {
      print_error ("Could not create thread pool conditional semaphore\n");
      return (S_FAILURE);
    }

  thread_pool->threads.prev = &thread_pool->threads;
  thread_pool->threads.next = &thread_pool->threads;
  thread_pool->threads.thread = pthread_self ();

  return (S_SUCCESS);
}

static void
thread_cleanup (void *arg)
{
  thread_cleanup_context_t *tcc = arg;
  node_t *node = tcc->node;
  thread_pool_t *thread_pool = tcc->thread_pool;

  pthread_mutex_lock (&thread_pool->mutex);
  node->prev->next = node->next;
  node->next->prev = node->prev;
  pthread_mutex_unlock (&thread_pool->mutex);

  free (node);

  pthread_cond_signal (&thread_pool->cond);
}

// TODO: Add status checks and cleanup in case of errors
static void *
thread_run (void *arg)
{
  tp_context_t *tp_ctx = (tp_context_t *)arg;
  tp_context_t local_ctx = *tp_ctx;

  if (pthread_mutex_unlock (&tp_ctx->mutex) != 0)
    {
      print_error ("Could not unlock mutex\n");
      return (NULL);
    }

  node_t *node = (node_t *)calloc (1, sizeof (*node));
  if (!node)
    {
      print_error ("Could not allocate memory\n");
      return (NULL);
    }

  node->thread = pthread_self ();
  pthread_detach (node->thread);

  pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

  if (pthread_mutex_lock (&local_ctx.thread_pool->mutex) != 0)
    {
      free (node);
      print_error ("Could not lock mutex\n");
      return (NULL);
    }

  node->next = &local_ctx.thread_pool->threads;
  node->prev = local_ctx.thread_pool->threads.prev;

  local_ctx.thread_pool->threads.prev->next = node;
  local_ctx.thread_pool->threads.prev = node;

  pthread_mutex_unlock (&local_ctx.thread_pool->mutex);

  thread_cleanup_context_t tcc
      = { .thread_pool = local_ctx.thread_pool, .node = node };
  pthread_cleanup_push (thread_cleanup, &tcc);

  pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);

  local_ctx.func (local_ctx.arg);

  pthread_cleanup_pop (!0);

  return (NULL);
}

status_t
thread_create (thread_pool_t *thread_pool, void *(*func) (void *), void *arg)
{
  tp_context_t context
      = { .thread_pool = thread_pool, .func = func, .arg = arg };

  if (pthread_mutex_init (&context.mutex, NULL) != 0)
    {
      print_error ("Could not create mutex\n");
      return (S_FAILURE);
    }

  if (pthread_mutex_lock (&context.mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (S_FAILURE);
    }

  pthread_t thread;
  if (pthread_create (&thread, NULL, &thread_run, &context) != 0)
    {
      print_error ("Could not create thread\n");
      return (S_FAILURE);
    }

  if (pthread_mutex_lock (&context.mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
thread_pool_cancel (thread_pool_t *thread_pool)
{
  pthread_t self_id = pthread_self ();

  for (;;)
    {
      if (pthread_mutex_lock (&thread_pool->mutex) != 0)
        {
          print_error ("Could not lock mutex\n");
          return (S_FAILURE);
        }

      pthread_t thread = thread_pool->threads.next->thread;
      bool empty = (thread_pool->threads.next == &thread_pool->threads);

      if (pthread_mutex_unlock (&thread_pool->mutex) != 0)
        {
          print_error ("Could not unlock mutex\n");
          return (S_FAILURE);
        }

      if (empty)
        break;

      if (thread == self_id)
        return (S_FAILURE);

      pthread_cancel (thread);

      pthread_mutex_lock (&thread_pool->mutex);
      while (thread_pool->threads.next->thread == thread)
        pthread_cond_wait (&thread_pool->cond, &thread_pool->mutex);
      pthread_mutex_unlock (&thread_pool->mutex);
    }

  return (S_SUCCESS);
}

int
create_threads (thread_pool_t *thread_pool, int number_of_threads,
                void *func (void *), void *context)
{
  int active_threads = 0;
  for (int i = 0; i < number_of_threads; ++i)
    if (thread_create (thread_pool, func, context) == S_SUCCESS)
      ++active_threads;

  if (active_threads == 0)
    print_error ("Could not create a single thread\n");

  return (active_threads);
}
