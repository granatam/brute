#ifndef BRUTE_H
#define BRUTE_H

#include "common.h"
#include "config.h"

#include <stdbool.h>

bool brute (task_t *task, config_t *config,
            password_handler_t password_handler, void *context);

#endif // BRUTE_H
