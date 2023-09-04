#include "multi.h"

#include "brute.h"
#include "queue.h"
#include "single.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* TODO: Need a better function name */
void *
mt_password_check (void *context)
{
  mt_context_t *mt_context = (mt_context_t *)context;
  task_t task;
  /* while (true)? signal semaphore? */
  while (true)
    {
      queue_pop (&mt_context->queue, &task);
      if (st_password_check (&task, &mt_context->config->hash))
        {
          memcpy (mt_context->result, task.password, sizeof (task.password));
        }
    }
  return NULL;
}

bool
queue_push_wrapper (task_t *task, void *context)
{
  mt_context_t *mt_context = (mt_context_t *)context;
  queue_push (&mt_context->queue, task);

  return task->password[0] != 0;
}

bool
run_multi (task_t *task, config_t *config)
{
  mt_context_t context;
  queue_init (&context.queue);
  context.config = config;

  int number_of_cpus = sysconf (_SC_NPROCESSORS_ONLN);
  pthread_t threads[number_of_cpus];
  for (int i = 0; i < number_of_cpus; ++i)
    {
      pthread_create (&threads[i], NULL, mt_password_check, (void *)&context);
    }

  bool is_found = false;
  switch (config->brute_mode)
    {
    case BM_ITER:
      is_found = brute_iter (task, config, queue_push_wrapper, &context);
      break;
    case BM_RECU:
      is_found
          = brute_rec_wrapper (task, config, queue_push_wrapper, &context);
      break;
    }

  for (int i = 0; i < number_of_cpus; ++i)
    {
      pthread_join (threads[i], NULL);
    }

  return is_found;
}
