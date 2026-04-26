#ifndef BRUTE_ENGINE_H
#define BRUTE_ENGINE_H

#include "common.h"
#include "queue.h"

#include <pthread.h>
#include <stdbool.h>

typedef struct brute_engine_t
{
  queue_t task_queue;
  password_t password;
  bool found;
  int tasks_remaining;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} brute_engine_t;

status_t brute_engine_init (brute_engine_t *engine);
status_t brute_engine_cancel (brute_engine_t *engine);
status_t brute_engine_destroy (brute_engine_t *engine);

queue_status_t brute_engine_submit_task (brute_engine_t *engine, task_t *task);
queue_status_t brute_engine_take_task (brute_engine_t *engine, task_t *task);
queue_status_t brute_engine_try_take_task (brute_engine_t *engine,
                                           task_t *task);
queue_status_t brute_engine_return_task (brute_engine_t *engine, task_t *task);

status_t brute_engine_report_result (brute_engine_t *engine,
                                     password_t password);

status_t brute_engine_task_done (brute_engine_t *engine);
status_t brute_engine_wait (brute_engine_t *engine);

bool brute_engine_has_result (brute_engine_t *engine);
bool brute_engine_copy_result (brute_engine_t *engine, password_t out);

#endif // BRUTE_ENGINE_H
