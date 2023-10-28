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

  if (pthread_mutex_lock (&local_context.thread_pool->mutex) != 0)
    {
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

  local_context.func (local_context.arg);

  if (pthread_mutex_lock (&local_context.thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (NULL);
    }

  node->prev->next = node->next;
  node->next->prev = node->prev;

  if (pthread_mutex_unlock (&local_context.thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (NULL);
    }

  free (node);

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
      pthread_join (thread, NULL);
    }

  return (S_SUCCESS);
}

