#ifndef REACTOR_COMMON_H
#define REACTOR_COMMON_H

#include "common.h"
#include "config.h"
#include "queue.h"

typedef struct reactor_context_t
{
  queue_t jobs_queue;
  struct event_base *ev_base;
} reactor_context_t;

typedef struct job_t
{
  void *arg;
  status_t (*job_func) (void *);
} job_t;

void *handle_io (void *arg);
void *dispatch_event_loop (void *arg);

#endif // REACTOR_COMMON_H
