#include "multi.h"

#include "queue.h"
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

bool
mt_password_handler (password_t password, void *context)
{
  mt_context_t *mt_context = (mt_context_t *)context;
  task_t task = {
    .password = password,
  };

  queue_push (&mt_context->queue,
              &task); /* memory leak I guess, need to refactor brute_X funcs */

  return false;
}

bool
run_multi (password_t password, config_t *config)
{
  queue_t queue;
  password_t result;
  mt_context_t context = {
    .queue = queue,
    .config = &config,
    .result = result,
  };

  queue_init (&context.queue);

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
      found = brute_iter (password, config, mt_password_handler, &context);
    case BM_RECU:
      found = brute_rec_wrapper (password, config, mt_password_handler,
                                 &context);
    }

  for (int i = 0; i < number_of_cpus; ++i)
    {
      pthread_join (threads[i], NULL);
    }

  return found;
}
