#ifndef BRUTE_H
#define BRUTE_H

#include <stdbool.h>
#include "config.h"
#include "common.h"

bool brute_rec (char *password, config_t *config,
                password_handler_t password_handler, void *context, int pos);
bool brute_rec_wrapper (char *password, config_t *config,
                        password_handler_t password_handler, void *context);
bool brute_iter (char *password, config_t *config,
                 password_handler_t password_handler, void *context);

#endif
