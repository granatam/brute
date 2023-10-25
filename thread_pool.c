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

  return (S_SUCCESS);
}

status_t
thread_pool_insert (thread_pool_t *thread_pool, pthread_t *thread,
                    void *(*func) (void *), void *arg)
{
  if (pthread_mutex_lock (&thread_pool->mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (S_FAILURE);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &thread_pool->mutex);

  node_t *node = (node_t *)malloc (sizeof (node_t));
  if (!node)
    {
      print_error ("Could not allocate memory for thread %d\n", thread);
      return (S_FAILURE);
    }

  node->thread = thread;
  node->next = NULL;

  if (!thread_pool->data)
    {
      node->prev = NULL;
      thread_pool->data = node;
    }
  else
    {
      node_t *tmp_node = thread_pool->data;
      while (tmp_node->next)
        tmp_node = tmp_node->next;

      tmp_node->next = node;
      node->prev = tmp_node;
    }

  if (pthread_create (node->thread, NULL, func, arg) != 0)
    {
      print_error ("Could not create thread %d\n", node->thread);
      return (S_FAILURE);
    }

  pthread_cleanup_pop (!0);

  return (S_SUCCESS);
}

status_t
thread_pool_remove (thread_pool_t *thread_pool, pthread_t thread)
{
  if (!thread_pool->data)
    {
      print_error ("Thread pool is empty\n");
      return (S_FAILURE);
    }

  node_t *tmp_node = thread_pool->data;
  while (tmp_node->next)
    {
      if (*tmp_node->thread == thread)
        {
          if (tmp_node->next)
            tmp_node->next->prev = tmp_node->prev;

          if (tmp_node->prev)
            tmp_node->prev->next = tmp_node->next;

          pthread_cancel (thread);
          pthread_join (thread, NULL);

          free (tmp_node->thread);
          free (tmp_node);

          return (S_SUCCESS);
        }

      tmp_node = tmp_node->next;
    }

  print_error ("Could not find thread in thread pool\n");

  return (S_FAILURE);
}

status_t
thread_pool_cancel (thread_pool_t *thread_pool)
{
  node_t *node = thread_pool->data;
  if (!node)
    {
      print_error ("Thread pool is empty\n");
      return (S_FAILURE);
    }

  while (node->next)
    {
      pthread_t thread_id = *node->thread;

      pthread_cancel (thread_id);
      pthread_join (thread_id, NULL);

      free (node->thread);

      node = node->next;
      free (node->prev);
    }

  free (node);

  return (S_SUCCESS);
}
