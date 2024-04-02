#include "queue.h"

queue_status_t
queue_init (queue_t *queue)
{
  queue->head = queue->tail = 0;
  queue->active = true;

  if (sem_init (&queue->full, 0, 0) != 0)
    goto fail;

  if (sem_init (&queue->empty, 0, QUEUE_SIZE) != 0)
    goto fail;

  if (pthread_mutex_init (&queue->head_mutex, NULL) != 0)
    goto fail;

  if (pthread_mutex_init (&queue->tail_mutex, NULL) != 0)
    goto fail;

  return (QS_SUCCESS);

fail:
  queue->active = false;
  return (QS_FAILURE);
}

queue_status_t
queue_push (queue_t *queue, task_t *task)
{
  if (sem_wait (&queue->empty) != 0)
    goto fail;

  // TODO: write it better
  if (!queue->active)
    {
      sem_post (&queue->empty);
      return (QS_INACTIVE);
    }

  if (pthread_mutex_lock (&queue->tail_mutex) != 0)
    goto fail;

  queue->queue[queue->tail] = *task;
  queue->tail = (queue->tail + 1) % QUEUE_SIZE;

  if (pthread_mutex_unlock (&queue->tail_mutex) != 0)
    goto fail;

  if (sem_post (&queue->full) != 0)
    goto fail;

  return (QS_SUCCESS);

fail:
  queue->active = false;
  return (QS_FAILURE);
}

queue_status_t
queue_pop (queue_t *queue, task_t *task)
{
  if (sem_wait (&queue->full) != 0)
    goto fail;

  if (!queue->active)
    {
      sem_post (&queue->full);
      return (QS_INACTIVE);
    }

  if (pthread_mutex_lock (&queue->head_mutex) != 0)
    goto fail;

  *task = queue->queue[queue->head];
  queue->head = (queue->head + 1) % QUEUE_SIZE;

  if (pthread_mutex_unlock (&queue->head_mutex) != 0)
    goto fail;

  if (sem_post (&queue->empty) != 0)
    goto fail;

  return (QS_SUCCESS);

fail:
  queue->active = false;
  return (QS_FAILURE);
}

queue_status_t
queue_cancel (queue_t *queue)
{
  queue->active = false;

  if (sem_post (&queue->full) != 0)
    return (QS_FAILURE);

  if (sem_post (&queue->empty) != 0)
    return (QS_FAILURE);

  return (QS_SUCCESS);
}

queue_status_t
queue_destroy (queue_t *queue)
{
  queue->active = false;
  queue->head = queue->tail = 0;

  if (sem_destroy (&queue->full) != 0)
    return (QS_FAILURE);

  if (sem_destroy (&queue->empty) != 0)
    return (QS_FAILURE);

  if (pthread_mutex_destroy (&queue->head_mutex) != 0)
    return (QS_FAILURE);

  if (pthread_mutex_destroy (&queue->tail_mutex) != 0)
    return (QS_FAILURE);

  return (QS_SUCCESS);
}
