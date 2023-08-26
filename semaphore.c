#include "semaphore.h"

int
sem_init (sem_t *sem, int pshared, unsigned int value)
{
  (void)pshared; /* to suppress the "unused parameter" warning */
  sem->counter = value;
  if (pthread_cond_init (&sem->cond_sem, NULL) != 0)
    {
      return -1;
    }

  if (pthread_mutex_init (&sem->mutex, NULL) != 0)
    {
      return -1;
    }

  return 0;
}

int
sem_post (sem_t *sem)
{
  if (pthread_mutex_lock (&sem->mutex) != 0)
    {
      return -1;
    }
  if (sem->counter++ == 0)
    {
      if (pthread_cond_signal (&sem->cond_sem) != 0)
        {
          return -1;
        }
    }
  if (pthread_mutex_unlock (&sem->mutex) != 0)
    {
      return -1;
    }

  return 0;
}

int
sem_wait (sem_t *sem)
{
  if (pthread_mutex_lock (&sem->mutex) != 0)
    {
      return -1;
    }
  while (sem->counter == 0)
    {
      if (pthread_cond_wait (&sem->cond_sem, &sem->mutex) != 0)
        {
          return -1;
        }
    }
  --sem->counter;
  if (pthread_mutex_unlock (&sem->mutex) != 0)
    {
      return -1;
    }

  return 0;
}