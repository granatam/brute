#include "brute_engine.h"

#include "brute.h"
#include "log.h"

#include <pthread.h>
#include <string.h>

status_t
brute_engine_init (brute_engine_t *engine)
{
  memset (engine, 0, sizeof (*engine));

  if (queue_init (&engine->task_queue, sizeof (task_t)) != QS_SUCCESS)
    {
      error ("Could not initialize brute engine task queue");
      return (S_FAILURE);
    }

  if (pthread_mutex_init (&engine->mutex, NULL) != 0)
    {
      error ("Could not initialize brute engine mutex");
      queue_destroy (&engine->task_queue);
      return (S_FAILURE);
    }

  if (pthread_cond_init (&engine->cond, NULL) != 0)
    {
      error ("Could not initialize brute engine condition variable");
      pthread_mutex_destroy (&engine->mutex);
      queue_destroy (&engine->task_queue);
      return (S_FAILURE);
    }

  engine->tasks_remaining = 0;
  engine->found = false;
  memset (engine->password, 0, sizeof (engine->password));

  return (S_SUCCESS);
}

status_t
brute_engine_cancel (brute_engine_t *engine)
{
  if (queue_cancel (&engine->task_queue) != QS_SUCCESS)
    {
      error ("Could not cancel brute engine task queue");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
brute_engine_destroy (brute_engine_t *engine)
{
  status_t status = S_SUCCESS;

  if (queue_destroy (&engine->task_queue) != QS_SUCCESS)
    {
      error ("Could not destroy brute engine task queue");
      status = S_FAILURE;
    }

  if (pthread_cond_destroy (&engine->cond) != 0)
    {
      error ("Could not destroy brute engine condition variable");
      status = S_FAILURE;
    }

  if (pthread_mutex_destroy (&engine->mutex) != 0)
    {
      error ("Could not destroy brute engine mutex");
      status = S_FAILURE;
    }

  return (status);
}

queue_status_t
brute_engine_submit_task (brute_engine_t *engine, task_t *task)
{
  if (pthread_mutex_lock (&engine->mutex) != 0)
    {
      error ("Could not lock brute engine mutex");
      return (QS_FAILURE);
    }

  ++engine->tasks_remaining;

  if (pthread_mutex_unlock (&engine->mutex) != 0)
    {
      error ("Could not unlock brute engine mutex");
      return (QS_FAILURE);
    }

  queue_status_t status = queue_push (&engine->task_queue, task);
  if (status != QS_SUCCESS)
    {
      if (status == QS_FAILURE)
        error ("Could not submit task to brute engine");

      if (pthread_mutex_lock (&engine->mutex) == 0)
        {
          --engine->tasks_remaining;
          pthread_mutex_unlock (&engine->mutex);
        }
    }

  return (status);
}

queue_status_t
brute_engine_take_task (brute_engine_t *engine, task_t *task)
{
  return (queue_pop (&engine->task_queue, task));
}

queue_status_t
brute_engine_try_take_task (brute_engine_t *engine, task_t *task)
{
  return (queue_trypop (&engine->task_queue, task));
}

queue_status_t
brute_engine_return_task (brute_engine_t *engine, task_t *task)
{
  return (queue_push_back (&engine->task_queue, task));
}

status_t
brute_engine_report_result (brute_engine_t *engine, password_t password)
{
  if (pthread_mutex_lock (&engine->mutex) != 0)
    {
      error ("Could not lock brute engine mutex");
      return (S_FAILURE);
    }

  if (!engine->found)
    {
      engine->found = true;
      memcpy (engine->password, password, sizeof (engine->password));

      if (queue_cancel (&engine->task_queue) != QS_SUCCESS)
        error ("Could not cancel brute engine task queue after result");

      if (pthread_cond_signal (&engine->cond) != 0)
        {
          error ("Could not signal brute engine condition variable");
          pthread_mutex_unlock (&engine->mutex);
          return (S_FAILURE);
        }
    }

  if (pthread_mutex_unlock (&engine->mutex) != 0)
    {
      error ("Could not unlock brute engine mutex");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

status_t
brute_engine_task_done (brute_engine_t *engine)
{
  if (pthread_mutex_lock (&engine->mutex) != 0)
    {
      error ("Could not lock brute engine mutex");
      return (S_FAILURE);
    }

  status_t status = S_SUCCESS;

  if (engine->tasks_remaining > 0)
    --engine->tasks_remaining;

  if (engine->tasks_remaining == 0 || engine->found)
    {
      if (pthread_cond_signal (&engine->cond) != 0)
        {
          error ("Could not signal brute engine condition variable");
          status = S_FAILURE;
        }
    }

  if (pthread_mutex_unlock (&engine->mutex) != 0)
    {
      error ("Could not unlock brute engine mutex");
      return (S_FAILURE);
    }

  return (status);
}

status_t
brute_engine_wait (brute_engine_t *engine)
{
  if (pthread_mutex_lock (&engine->mutex) != 0)
    {
      error ("Could not lock brute engine mutex");
      return (S_FAILURE);
    }

  status_t status = S_SUCCESS;

  while (engine->tasks_remaining != 0 && !engine->found)
    if (pthread_cond_wait (&engine->cond, &engine->mutex) != 0)
      {
        error ("Could not wait on brute engine condition variable");
        status = S_FAILURE;
        break;
      }

  trace (
      "Brute engine unblocked: either all tasks completed or password found");

  if (pthread_mutex_unlock (&engine->mutex) != 0)
    {
      error ("Could not unlock brute engine mutex");
      return (S_FAILURE);
    }

  return (status);
}

bool
brute_engine_has_result (brute_engine_t *engine)
{
  if (pthread_mutex_lock (&engine->mutex) != 0)
    {
      error ("Could not lock brute engine mutex");
      return (false);
    }

  bool found = engine->found;

  if (pthread_mutex_unlock (&engine->mutex) != 0)
    {
      error ("Could not unlock brute engine mutex");
      return (false);
    }

  return (found);
}

bool
brute_engine_copy_result (brute_engine_t *engine, password_t out)
{
  if (pthread_mutex_lock (&engine->mutex) != 0)
    {
      error ("Could not lock brute engine mutex");
      return (false);
    }

  bool found = engine->found;
  if (found)
    memcpy (out, engine->password, sizeof (engine->password));

  if (pthread_mutex_unlock (&engine->mutex) != 0)
    {
      error ("Could not unlock brute engine mutex");
      return (false);
    }

  return (found);
}

static bool
submit_task_cb (task_t *task, void *context)
{
  brute_engine_t *engine = context;

  if (brute_engine_submit_task (engine, task) == QS_FAILURE)
    return false;

  return brute_engine_has_result (engine);
}

status_t
brute_engine_run (brute_engine_t *engine, task_t *task, config_t *config,
                  bool *found)
{
  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  brute (task, config, submit_task_cb, engine);

  if (brute_engine_wait (engine) == S_FAILURE)
    return S_FAILURE;

  *found = brute_engine_copy_result (engine, task->result.password);

  return S_SUCCESS;
}
