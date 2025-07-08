#include "queue.h"

#include "common.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

queue_status_t
queue_init (queue_t *queue, size_t unit_size)
{
  if (!(queue->queue = calloc (QUEUE_SIZE, unit_size)))
    goto fail;

  queue->unit_size = unit_size;
  queue->head = queue->tail = 0;
  queue->active = true;
  queue->list.prev = &queue->list;
  queue->list.next = &queue->list;

  if (sem_init (&queue->full, 0, 0) != SS_SUCCESS)
    goto fail;

  if (sem_init (&queue->empty, 0, QUEUE_SIZE) != SS_SUCCESS)
    goto fail;

  if (pthread_mutex_init (&queue->head_mutex, NULL) != 0)
    goto fail;

  if (pthread_mutex_init (&queue->tail_mutex, NULL) != 0)
    goto fail;

  return (QS_SUCCESS);

fail:
  trace ("Failed to initialize queue");
  queue->active = false;
  return (QS_FAILURE);
}

static queue_status_t
queue_push_internal (queue_t *queue, void *payload)
{
  if (!queue->active)
    {
      sem_post (&queue->empty);
      return (QS_INACTIVE);
    }

  if (pthread_mutex_lock (&queue->tail_mutex) != 0)
    goto fail;

  memcpy ((char *)queue->queue + (queue->tail * queue->unit_size), payload,
          queue->unit_size);
  queue->tail = (queue->tail + 1) % QUEUE_SIZE;

  if (pthread_mutex_unlock (&queue->tail_mutex) != 0)
    goto fail;

  if (sem_post (&queue->full) != SS_SUCCESS)
    goto fail;

  return (QS_SUCCESS);

fail:
  queue->active = false;
  return (QS_FAILURE);
}

queue_status_t
queue_push (queue_t *queue, void *payload)
{
  if (sem_wait (&queue->empty) != SS_SUCCESS)
    {
      queue->active = false;
      return (QS_FAILURE);
    }

  return (queue_push_internal (queue, payload));
}

static void
cleanup_free_handler (void *ptr)
{
  if (*(void **)ptr)
    {
      free (*(void **)ptr);
      *(void **)ptr = NULL;
    }
}

static queue_status_t
queue_pop_internal (queue_t *queue, void *payload)
{
  ll_node_t *node_to_pop = NULL;
  if (!queue->active)
    {
      sem_post (&queue->full);
      return (QS_INACTIVE);
    }

  pthread_cleanup_push (cleanup_free_handler, &node_to_pop);
  if (pthread_mutex_lock (&queue->head_mutex) != 0)
    goto fail;

  if (queue->list.next != &queue->list)
    {
      node_to_pop = queue->list.next;
      memcpy (payload, node_to_pop->payload, queue->unit_size);

      queue->list.next = node_to_pop->next;
      node_to_pop->next->prev = &queue->list;
    }
  else
    {
      memcpy (payload, (char *)queue->queue + (queue->head * queue->unit_size),
              queue->unit_size);
      queue->head = (queue->head + 1) % QUEUE_SIZE;
    }

  if (pthread_mutex_unlock (&queue->head_mutex) != 0)
    goto fail;

  if (sem_post (&queue->empty) != SS_SUCCESS)
    goto fail;

  pthread_cleanup_pop (!0);

  return (QS_SUCCESS);

fail:
  if (node_to_pop)
    free (node_to_pop);
  queue->active = false;
  return (QS_FAILURE);
}

queue_status_t
queue_pop (queue_t *queue, void *payload)
{
  if (sem_wait (&queue->full) != SS_SUCCESS)
    {
      queue->active = false;
      return (QS_FAILURE);
    }

  return queue_pop_internal (queue, payload);
}

queue_status_t
queue_trypop (queue_t *queue, void *payload)
{
  switch (sem_trywait (&queue->full))
    {
    case SS_LOCKED:
      return (QS_EMPTY);
    case SS_FAILURE:
      return (QS_FAILURE);
    case SS_SUCCESS:
      return (queue_pop_internal (queue, payload));
    }
}

queue_status_t
queue_cancel (queue_t *queue)
{
  queue->active = false;

  if (sem_post (&queue->full) != SS_SUCCESS)
    return (QS_FAILURE);

  if (sem_post (&queue->empty) != SS_SUCCESS)
    return (QS_FAILURE);

  return (QS_SUCCESS);
}

queue_status_t
queue_destroy (queue_t *queue)
{
  queue->active = false;
  queue->head = queue->tail = 0;

  free (queue->queue);

  if (sem_destroy (&queue->full) != SS_SUCCESS)
    return (QS_FAILURE);

  if (sem_destroy (&queue->empty) != SS_SUCCESS)
    return (QS_FAILURE);

  if (pthread_mutex_destroy (&queue->head_mutex) != 0)
    return (QS_FAILURE);

  if (pthread_mutex_destroy (&queue->tail_mutex) != 0)
    return (QS_FAILURE);

  while (queue->list.next != &queue->list)
    {
      ll_node_t *node = queue->list.next;
      queue->list.next = node->next;
      node->next->prev = &queue->list;
      free (node);
    }

  return (QS_SUCCESS);
}

queue_status_t
queue_push_back (queue_t *queue, void *payload)
{
  trace ("Calling queue_push_back");
  queue_status_t status = QS_SUCCESS;

  ll_node_t *node
      = calloc (1, sizeof (*node) + queue->unit_size - sizeof (node->payload));
  if (!node)
    {
      error ("Could not allocate memory");
      return (QS_FAILURE);
    }
  pthread_cleanup_push (free, node);
  memcpy (node->payload, payload, queue->unit_size);

  volatile bool list_used = true;
  if (pthread_mutex_lock (&queue->head_mutex) != 0)
    {
      error ("Could not lock queue head mutex");
      free (node);
      return (QS_FAILURE);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &queue->head_mutex);

  if (sem_trywait (&queue->empty) == SS_SUCCESS)
    {
      status = queue_push_internal (queue, payload);
      list_used = false;
    }
  else
    {
      node->next = &queue->list;
      node->prev = queue->list.prev;

      queue->list.prev->next = node;
      queue->list.prev = node;
      status
          = (sem_post (&queue->full) == SS_SUCCESS) ? QS_SUCCESS : QS_FAILURE;
    }
  pthread_cleanup_pop (!0);

  if (!list_used)
    free (node);
  pthread_cleanup_pop (0);

  return (status);
}
