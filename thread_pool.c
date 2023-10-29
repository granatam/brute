#include "thread_pool.h"
#include "common.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

status_t
thread_pool_init (thread_pool_t *thread_pool)
{
  if (pthread_mutex_init (&thread_pool->mutex, NULL) != 0)
    {
      print_error ("Could not create thread pool mutex\n");
      return (S_FAILURE);
    }

  thread_pool->threads.prev = &thread_pool->threads;
  thread_pool->threads.next = &thread_pool->threads;

  return (S_SUCCESS);
}

static void
thread_cleanup (void *arg)
{
  thread_cleanup_context_t *tcc = arg;
  node_t *node = tcc->node;
  thread_pool_t *thread_pool = tcc->thread_pool;

  if (pthread_mutex_lock (&thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
    }

  node->prev->next = node->next;
  node->next->prev = node->prev;

  if (pthread_mutex_unlock (&thread_pool->mutex) != 0)
    {
      print_error ("Could not unlock mutex\n");
    }

  if (pthread_detach (node->thread) != 0)
    {
      print_error ("Could not detach thread\n");
    }

  free (node);
}

static void *
thread_run (void *arg)
{
  tp_context_t *context = (tp_context_t *)arg;
  tp_context_t local_context = *context;

  if (pthread_mutex_unlock (&context->mutex) != 0)
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

  pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

  if (pthread_mutex_lock (&local_context.thread_pool->mutex) != 0)
    {
      free (node);
      print_error ("Could not lock mutex\n");
      return (NULL);
    }

  node->next = &local_context.thread_pool->threads;
  node->prev = local_context.thread_pool->threads.prev;

  local_context.thread_pool->threads.prev->next = node;
  local_context.thread_pool->threads.prev = node;

  if (pthread_mutex_unlock (&local_context.thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (NULL);
    }

  thread_cleanup_context_t tcc
      = { .thread_pool = local_context.thread_pool, .node = node };
  pthread_cleanup_push (thread_cleanup, &tcc);

  pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);

  local_context.func (local_context.arg);

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

      if (pthread_cancel (thread) != 0)
        {
          print_error ("Could not cancel thread\n");
          return (S_FAILURE);
        }

      if (pthread_join (thread, NULL) != 0)
        {
          print_error ("Could not join thread\n");
          return (S_FAILURE);
        }
    }

  return (S_SUCCESS);
}
