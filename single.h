#ifndef SINGLE_H
#define SINGLE_H

#include "common.h"
#include "config.h"
#include <stdbool.h>

bool st_password_handler(task_t *task, void *context);
bool run_single(task_t *task, config_t *config);

#endif // SINGLE_H
