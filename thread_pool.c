#include "thread_pool.h"
#include "common.h"

#include <pthread.h>
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

  local_context.node->thread = pthread_self ();

  if (pthread_mutex_lock (&local_context.thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (NULL);
    }

  local_context.node->next = &local_context.thread_pool->threads;
  local_context.node->prev = local_context.thread_pool->threads.prev;

  local_context.thread_pool->threads.prev->next = local_context.node;
  local_context.thread_pool->threads.prev = local_context.node;

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

  local_context.node->prev->next = local_context.node->next;
  local_context.node->next->prev = local_context.node->prev;

  if (pthread_mutex_unlock (&local_context.thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (NULL);
    }

  free (local_context.node);

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

  node_t *node = (node_t *)calloc (1, sizeof (*node));
  if (!node)
    {
      print_error ("Could not allocate memory\n");
      return (S_FAILURE);
    }

  context.node = node;

  if (pthread_mutex_unlock (&context.mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (S_FAILURE);
    }

  pthread_t thread;
  if (pthread_create (&thread, NULL, &thread_run, &context) != 0)
    {
      print_error ("Could not create thread\n");
      free (node);

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
  node_t *current = thread_pool->threads.next;

  if (pthread_mutex_lock (&thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (S_FAILURE);
    }

  while (current != &thread_pool->threads)
    {
      if (current->thread == self_id)
        continue;

      node_t *tmp_node = current;
      current = current->next;

      if (pthread_mutex_unlock (&thread_pool->mutex) != 0)
        {
          print_error ("Could not unlock mutex\n");
          return (S_FAILURE);
        }

      pthread_cancel (tmp_node->thread);
      pthread_join (tmp_node->thread, NULL);

      if (pthread_mutex_lock (&thread_pool->mutex) != 0)
        {
          print_error ("Could not lock mutex\n");
          return (S_FAILURE);
        }
    }

  thread_pool->threads.next = &thread_pool->threads;
  thread_pool->threads.prev = &thread_pool->threads;

  return (S_SUCCESS);
}
