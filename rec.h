#ifndef REC_H
#define REC_H

#include "common.h"
#include "config.h"

bool brute_rec (task_t *task, config_t *config,
                password_handler_t password_handler, void *context, int pos);
bool brute_rec_wrapper (task_t *task, config_t *config,
                        password_handler_t password_handler, void *context);

#endif // REC_H
