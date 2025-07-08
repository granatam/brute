#ifndef REACTOR_COMMON_H
#define REACTOR_COMMON_H

#include "common.h"
#include "config.h"
#include "queue.h"
#include "thread_pool.h"

#include <event2/event.h>

typedef struct reactor_context_t
{
  queue_t jobs_queue;
  struct event_base *ev_base;
} reactor_context_t;

status_t reactor_context_init (reactor_context_t *ctx);
void reactor_context_stop (reactor_context_t *ctx);
status_t reactor_context_destroy (reactor_context_t *ctx);
status_t reactor_context_drain_jobs (reactor_context_t *ctx);

typedef enum reactor_push_status_t
{
  RPS_SUCCESS,
  RPS_INACTIVE,
  RPS_FAILURE,
} reactor_push_status_t;

typedef void (*job_release_fn) (void *);

typedef struct job_t
{
  void *arg;
  status_t (*job_func) (void *);
  job_release_fn release_fn;
} job_t;

reactor_push_status_t reactor_push_job (reactor_context_t *rctr_ctx, void *arg,
                                        status_t (*job_func) (void *),
                                        job_release_fn release_fn);

typedef void (*reactor_event_visit_fn) (const struct event *ev, void *arg);

status_t reactor_for_each_event_snapshot (reactor_context_t *ctx,
                                          reactor_event_visit_fn visit,
                                          void *arg);
status_t reactor_event_del_free (struct event *ev);

status_t reactor_create_threads (thread_pool_t *tp, config_t *config,
                                 reactor_context_t *ptr);

typedef enum reactor_io_status
{
  RIO_DONE,
  RIO_PENDING,
  RIO_CLOSED,
  RIO_ERROR,
} reactor_io_status_t;

typedef struct reactor_conn_t
{
  struct event *read_event;
  evutil_socket_t fd;
} reactor_conn_t;

status_t reactor_conn_init (reactor_conn_t *conn, reactor_context_t *rctr_ctx,
                            evutil_socket_t fd, event_callback_fn on_read,
                            void *arg);
status_t reactor_conn_enable_read (reactor_conn_t *conn);
status_t reactor_conn_disable_read (reactor_conn_t *conn);
status_t reactor_conn_destroy (reactor_conn_t *conn);

reactor_io_status_t reactor_conn_readv (reactor_conn_t *conn,
                                        struct iovec *vec, int *vec_sz);

typedef struct io_state_t
{
  struct iovec vec[2];
  int32_t vec_sz;
  command_t cmd;
} io_state_t;

typedef struct write_state_t
{
  io_state_t base_state;
  struct iovec vec_extra[3];
  command_t cmd_extra;
  int32_t length;
  int32_t vec_extra_sz;
} write_state_t;

status_t write_state_write_wrapper (int socket_fd, struct iovec *vec,
                                    int *vec_sz);
status_t write_state_write (int socket_fd, write_state_t *write_state);

#endif // REACTOR_COMMON_H
