#include "thread_pool.h"
#include "common.h"

#include <stdlib.h>

status_t
thread_pool_init (thread_pool_t *thread_pool)
{
  if (pthread_mutex_init (&thread_pool->mutex, NULL) != 0)
    {
      print_error ("Could not create thread pool mutex\n");
      return (S_FAILURE);
    }

  thread_pool->data.prev = &thread_pool->data;
  thread_pool->data.next = &thread_pool->data;

  return (S_SUCCESS);
}

status_t
thread_create (thread_pool_t *thread_pool, pthread_t *thread,
               void *(*func) (void *), void *arg)
{
  // Should it be after pthread_mutex_lock?
  node_t *node = (node_t *)malloc (sizeof (node_t));
  if (!node)
    {
      print_error ("Could not allocate memory for thread %d\n", thread);
      return (S_FAILURE);
    }

  if (pthread_mutex_lock (&thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (S_FAILURE);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &thread_pool->mutex);

  node->thread = *thread;
  node->next = &thread_pool->data;
  node->prev = thread_pool->data.prev;

  thread_pool->data.prev->next = node;
  thread_pool->data.prev = node;

  pthread_cleanup_pop (!0);

  tp_context_t *context = (tp_context_t *)malloc (sizeof (*context));
  context->thread_pool = thread_pool;
  context->thread = node;
  context->func = func;
  context->arg = arg;

  if (pthread_create (thread, NULL, &thread_run, context) != 0)
    {
      print_error ("Could not create thread %d\n", *thread);
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

void *
thread_run (void *arg)
{
  tp_context_t *context = (tp_context_t *)arg;

  context->func (context->arg);

  if (pthread_mutex_lock (&context->thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (NULL);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &context->thread_pool->mutex);

  context->thread->prev->next = context->thread->next;
  context->thread->next->prev = context->thread->prev;

  pthread_cleanup_pop (!0);

  pthread_cancel (context->thread->thread);
  pthread_join (context->thread->thread, NULL);

  free (context->thread);
  free (context);

  return (NULL);
}

status_t
thread_pool_cancel (thread_pool_t *thread_pool)
{
  node_t *current = thread_pool->data.next;

  while (current != &thread_pool->data)
    {
      node_t *tmp_node = current;
      current = current->next;

      pthread_cancel (tmp_node->thread);
      pthread_join (tmp_node->thread, NULL);

      free (tmp_node);
    }

  thread_pool->data.next = &thread_pool->data;
  thread_pool->data.prev = &thread_pool->data;

  return (S_SUCCESS);
}
