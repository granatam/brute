#ifndef BRUTE_H
#define BRUTE_H

#include "common.h"
#include "config.h"
#include <stdbool.h>

bool brute_rec (task_t *task, config_t *config,
                password_handler_t password_handler, void *context, int pos);
bool brute_rec_wrapper (task_t *task, config_t *config,
                        password_handler_t password_handler, void *context);
bool brute_iter (task_t *task, config_t *config,
                 password_handler_t password_handler, void *context);

#endif // BRUTE_H
