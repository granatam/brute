#include "linked_list.h"

#include "common.h"
#include "log.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

status_t
linked_list_init (linked_list_t *list, size_t unit_size)
{
  if (pthread_mutex_init (&list->mutex, NULL) != 0)
    {
      error ("Could not create thread pool mutex");
      return (S_FAILURE);
    }

  if (pthread_cond_init (&list->cond, NULL) != 0)
    {
      error ("Could not create thread pool conditional semaphore");
      return (S_FAILURE);
    }

  list->nodes.prev = &list->nodes;
  list->nodes.next = &list->nodes;

  list->cancelled = false;
  list->count = 0;
  list->unit_size = unit_size;

  return (S_SUCCESS);
}

status_t
linked_list_push (linked_list_t *list, void *payload)
{
  if (pthread_mutex_lock (&list->mutex) != 0)
    {
      error ("Could not lock a mutex");
      return (S_FAILURE);
    }

  ll_node_t *node = calloc (1, sizeof (*node) + list->unit_size);

  if (!node)
    {
      error ("Could not allocate memory");
      pthread_mutex_unlock (&list->mutex);
      return (S_FAILURE);
    }

  memcpy (node->payload, payload, list->unit_size);
  ++list->count;

  if (list->cancelled)
    {
      --list->count;
      pthread_cond_signal (&list->cond);
      free (node);
      pthread_mutex_unlock (&list->mutex);

      return (S_FAILURE);
    }

  node->next = &list->nodes;
  node->prev = list->nodes.prev;

  list->nodes.prev->next = node;
  list->nodes.prev = node;

  if (pthread_mutex_unlock (&list->mutex) != 0)
    {
      error ("Could not unlock mutex");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
linked_list_pop (linked_list_t *list, void **payload)
{
  if (pthread_mutex_lock (&list->mutex) != 0)
    {
      error ("Could not lock a mutex");
      return (S_FAILURE);
    }
  pthread_cleanup_push (&cleanup_mutex_unlock, &list->mutex);

  if (list->nodes.next == &list->nodes)
    goto cleanup;

  ll_node_t *node_to_pop = list->nodes.prev;

  *payload = node_to_pop->payload;

  node_to_pop->prev->next = node_to_pop->next;
  node_to_pop->next->prev = node_to_pop->prev;
  --list->count;

cleanup:
  pthread_cleanup_pop (!0);

  return (S_SUCCESS);
}

status_t
linked_list_destroy (linked_list_t *list)
{
  assert (0 && "not implemented yet");
}
