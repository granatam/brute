#include "priority_queue.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define LCHILD(idx) (2 * idx + 1)
#define RCHILD(idx) (2 * idx + 2)
#define PARENT(idx) ((idx - 1) / 2)
#define HEAP_AT(pq, idx) ((char *)(pq)->heap + (idx) * (pq)->unit_size)

status_t
priority_queue_init (priority_queue_t *pq, size_t cap, size_t unit_size,
                     pq_comparator_t cmp)
{
  if (!(pq->heap = calloc (cap, unit_size)))
    return (S_FAILURE);
  pq->size = 0;
  pq->cap = cap;
  pq->cmp = cmp;
  pq->unit_size = unit_size;
  if (pthread_mutex_init (&pq->mutex, NULL) != 0)
    {
      free (pq->heap);
      pq->heap = NULL;
      return (S_FAILURE);
    }
  return (S_SUCCESS);
}

static void
swap (priority_queue_t *pq, size_t i, size_t p)
{
  char temp[pq->unit_size];
  memcpy (temp, HEAP_AT (pq, i), pq->unit_size);
  memcpy (HEAP_AT (pq, i), HEAP_AT (pq, p), pq->unit_size);
  memcpy (HEAP_AT (pq, p), temp, pq->unit_size);
}

status_t
priority_queue_push (priority_queue_t *pq, void *payload)
{
  status_t ret = S_FAILURE;
  pthread_mutex_lock (&pq->mutex);
  if (pq->size == pq->cap)
    goto unlock;

  memcpy (HEAP_AT (pq, pq->size), payload, pq->unit_size);
  size_t i = pq->size++;
  while (i > 0)
    {
      size_t p = PARENT (i);
      if (pq->cmp (HEAP_AT (pq, i), HEAP_AT (pq, p)) >= 0)
        break;
      swap (pq, i, p);
      i = p;
    }
  ret = S_SUCCESS;
unlock:
  pthread_mutex_unlock (&pq->mutex);
  return (ret);
}

status_t
priority_queue_top (priority_queue_t *pq, void *payload)
{
  status_t ret = S_FAILURE;
  pthread_mutex_lock (&pq->mutex);
  if (pq->size == 0)
    goto unlock;
  memcpy (payload, HEAP_AT (pq, 0), pq->unit_size);
  ret = S_SUCCESS;
unlock:
  pthread_mutex_unlock (&pq->mutex);
  return (ret);
}

status_t
priority_queue_pop (priority_queue_t *pq)
{
  status_t ret = S_FAILURE;
  pthread_mutex_lock (&pq->mutex);
  if (pq->size == 0)
    goto unlock;

  memcpy (pq->heap, HEAP_AT (pq, --pq->size), pq->unit_size);
  size_t i = 0;
  while (true)
    {
      size_t l = LCHILD (i), r = RCHILD (i);
      size_t smallest = i;

      if (l < pq->size && pq->cmp (HEAP_AT (pq, l), HEAP_AT (pq, i)) < 0)
        smallest = l;

      if (r < pq->size
          && pq->cmp (HEAP_AT (pq, r), HEAP_AT (pq, smallest)) < 0)
        smallest = r;

      if (smallest == i)
        break;

      swap (pq, i, smallest);
      i = smallest;
    }
  ret = S_SUCCESS;
unlock:
  pthread_mutex_unlock (&pq->mutex);
  return (ret);
}

void
priority_queue_destroy (priority_queue_t *pq)
{
  pthread_mutex_destroy (&pq->mutex);
  free (pq->heap);
  pq->heap = NULL;
  pq->size = 0;
}
