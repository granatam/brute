#ifndef REACTOR_SERVER_H
#define REACTOR_SERVER_H

#include "common.h"
#include "config.h"

#include <event2/util.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/uio.h>

bool run_reactor_server (task_t *, config_t *);

#endif // REACTOR_SERVER_H
