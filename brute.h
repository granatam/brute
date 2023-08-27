#ifndef BRUTE_H
#define BRUTE_H

#include "common.h"
#include "config.h"
#include <stdbool.h>

bool brute_rec (password_t password, config_t *config,
                password_handler_t password_handler, void *context, int pos);
bool brute_rec_wrapper (password_t password, config_t *config,
                        password_handler_t password_handler, void *context);
bool brute_iter (password_t password, config_t *config,
                 password_handler_t password_handler, void *context);

#endif // BRUTE_H
