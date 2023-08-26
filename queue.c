#include "queue.h"

int
queue_init (queue_t *queue)
{
  queue->head = queue->tail = 0;

  if (sem_init (&queue->full, 0, 0) != 0)
    {
      return -1;
    }
  if (sem_init (&queue->empty, 0, QUEUE_SIZE) != 0)
    {
      return -1;
    }

  if (pthread_mutex_init (&queue->head_mutex, NULL) != 0)
    {
      return -1;
    }
  if (pthread_mutex_init (&queue->tail_mutex, NULL) != 0)
    {
      return -1;
    }

  return 0;
}

int
queue_push (queue_t *queue, task_t *task)
{
  if (sem_wait (&queue->empty) != 0)
    {
      return -1;
    }
  if (pthread_mutex_lock (&queue->tail_mutex) != 0)
    {
      return -1;
    }
  queue->queue[queue->tail] = *task;
  queue->tail = (queue->tail + 1) % QUEUE_SIZE;
  if (pthread_mutex_unlock (&queue->tail_mutex) != 0)
    {
      return -1;
    }
  if (sem_post (&queue->full) != 0)
    {
      return -1;
    }

  return 0;
}

int
queue_pop (queue_t *queue, task_t *task)
{
  if (sem_wait (&queue->full) != 0)
    {
      return -1;
    }
  if (pthread_mutex_lock (&queue->head_mutex) != 0)
    {
      return -1;
    }
  *task = queue->queue[queue->head];
  queue->head = (queue->head + 1) % QUEUE_SIZE;
  if (pthread_mutex_unlock (&queue->head_mutex) != 0)
    {
      return -1;
    }
  if (sem_post (&queue->empty) != 0)
    {
      return -1;
    }

  return 0;
}
