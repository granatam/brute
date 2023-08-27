#ifndef SINGLE_H
#define SINGLE_H

#include "common.h"
#include "config.h"
#include <stdbool.h>

bool password_handler (password_t password, void *context);
bool run_single (password_t password, config_t *config);

#endif // SINGLE_H
