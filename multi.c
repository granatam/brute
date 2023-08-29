#include "multi.h"

#include "brute.h"
#include "queue.h"
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

bool
mt_password_handler (task_t *task, void *context)
{
  mt_context_t *mt_context = (mt_context_t *)context;
  queue_push (&mt_context->queue, task);

  return false;
}

bool
run_multi (task_t *task, config_t *config)
{
  queue_t queue;
  queue_init (&queue);
  password_t result;
  mt_context_t context = {
    .queue = queue,
    .config = config,
    .result = result,
  };


  int number_of_cpus = sysconf (_SC_NPROCESSORS_ONLN);
  pthread_t threads[number_of_cpus];
  for (int i = 0; i < number_of_cpus; ++i)
    {
      /* implement a function that will pop from the queue and check passwords
       */
      pthread_create (&threads[i], NULL, (void *)0, (void *)&context);
    }

  bool found = false;
  switch (config->brute_mode)
    {
    case BM_ITER:
      found = brute_iter (task, config, mt_password_handler, &context);
    case BM_RECU:
      found = brute_rec_wrapper (task, config, mt_password_handler, &context);
    }

  for (int i = 0; i < number_of_cpus; ++i)
    {
      pthread_join (threads[i], NULL);
    }

  return found;
}
