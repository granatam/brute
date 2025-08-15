#ifndef REACTOR_COMMON_H
#define REACTOR_COMMON_H

#include "common.h"
#include "config.h"
#include "queue.h"

typedef struct io_state_t
{
  struct iovec vec[2];
  size_t vec_sz;
  command_t cmd;
} io_state_t;

typedef struct write_state_t
{
  io_state_t base_state;
  struct iovec vec_extra[3];
  command_t cmd_extra;
  int32_t length;
  size_t vec_extra_sz;
} write_state_t;

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

status_t write_state_write_wrapper (int socket_fd, struct iovec *vec,
                                    size_t *vec_sz);
status_t write_state_write (int socket_fd, write_state_t *write_state);

status_t push_job (reactor_context_t *rctr_ctx, void *arg,
                   status_t (*job_func) (void *));

#endif // REACTOR_COMMON_H
