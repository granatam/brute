#ifndef REACTOR_COMMON_H
#define REACTOR_COMMON_H

#include "common.h"
#include "queue.h"
#include "thread_pool.h"

#include <event2/event.h>
#include <stdbool.h>

typedef status_t (*reactor_job_func_t) (void *arg);
typedef void (*reactor_job_cleanup_t) (void *arg);

typedef struct reactor_job_t
{
  void *arg;
  reactor_job_func_t job_func;
  reactor_job_cleanup_t cleanup;
} reactor_job_t;

typedef struct reactor_context_t
{
  struct event_base *ev_base;
  queue_t jobs_queue;
  bool shutting_down;
} reactor_context_t;

status_t reactor_context_init (reactor_context_t *ctx);
status_t reactor_context_stop (reactor_context_t *ctx);
status_t reactor_context_destroy (reactor_context_t *ctx);

queue_status_t reactor_push_job (reactor_context_t *ctx, reactor_job_t *job);

status_t create_reactor_threads (thread_pool_t *thread_pool, long io_threads,
                                 reactor_context_t *ctx);

#endif
