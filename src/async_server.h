#ifndef ASYNC_SERVER_H
#define ASYNC_SERVER_H

#include "common.h"
#include "config.h"

#include <stdbool.h>

bool run_async_server (task_t *task, config_t *config);

#endif // ASYNC_SERVER_H
