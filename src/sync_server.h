#ifndef SYNC_SERVER_H
#define SYNC_SERVER_H

#include "common.h"
#include "config.h"
#include "server_common.h"

#include <stdbool.h>

bool run_server (task_t *task, config_t *config);

#endif // SYNC_SERVER_H
