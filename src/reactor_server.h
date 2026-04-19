#ifndef REACTOR_SERVER_H
#define REACTOR_SERVER_H

#include "common.h"
#include "config.h"
#include "queue.h"
#include "server_common.h"

#include <event2/util.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>

typedef struct job_t
{
  void *arg;
  status_t (*job_func) (void *);
} job_t;

bool run_reactor_server (task_t *, config_t *);

#endif // REACTOR_SERVER_H
