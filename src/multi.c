#include "multi.h"

#include "brute.h"
#include "brute_engine.h"
#include "common.h"
#include "log.h"
#include "queue.h"
#include "single.h"
#include "thread_pool.h"

#include <pthread.h>
#include <string.h>

typedef struct mt_context_t
{
  brute_engine_t engine;
  config_t *config;
  thread_pool_t thread_pool;
} mt_context_t;

static status_t
mt_context_init (mt_context_t *context, config_t *config)
{
  if (brute_engine_init (&context->engine) == S_FAILURE)
    return S_FAILURE;

  if (thread_pool_init (&context->thread_pool) == S_FAILURE)
    {
      brute_engine_destroy (&context->engine);
      return S_FAILURE;
    }

  context->config = config;

  return S_SUCCESS;
}

static status_t
mt_context_destroy (mt_context_t *context)
{
  return (brute_engine_destroy (&context->engine));
}

static void *
mt_password_check (void *context)
{
  mt_context_t *mt_ctx = *(mt_context_t **)context;

  task_t task;
  st_context_t st_ctx = {
    .hash = mt_ctx->config->hash,
    .data = { .initialized = 0 },
  };

  while (true)
    {
      if (brute_engine_take_task (&mt_ctx->engine, &task) != QS_SUCCESS)
        return NULL;

      task.to = task.from;
      task.from = 0;

      if (brute (&task, mt_ctx->config, st_password_check, &st_ctx))
        {
          if (brute_engine_report_result (&mt_ctx->engine,
                                          task.result.password)
              == S_FAILURE)
            return NULL;
        }

      if (brute_engine_task_done (&mt_ctx->engine) == S_FAILURE)
        return NULL;
    }

  return NULL;
}

bool
run_multi (task_t *task, config_t *config)
{
  mt_context_t context;
  mt_context_t *context_ptr = &context;

  if (mt_context_init (&context, config) == S_FAILURE)
    return false;

  int active_threads = create_threads (
      &context.thread_pool, config->number_of_threads, mt_password_check,
      &context_ptr, sizeof (context_ptr), "mt worker");

  if (active_threads == 0)
    goto fail;

  bool found = false;
  if (brute_engine_run (&context.engine, task, config, &found) == S_FAILURE)
    goto fail;

  if (brute_engine_cancel (&context.engine) == S_FAILURE)
    error ("Could not cancel brute engine");

  if (thread_pool_join (&context.thread_pool) == S_FAILURE)
    error ("Could not join thread pool");

  if (mt_context_destroy (&context) == S_FAILURE)
    error ("Could not destroy context");

  return found;

fail:
  brute_engine_cancel (&context.engine);

  if (thread_pool_join (&context.thread_pool) == S_FAILURE)
    error ("Could not join thread pool");

  if (mt_context_destroy (&context) == S_FAILURE)
    error ("Could not destroy mt context");

  return false;
}
