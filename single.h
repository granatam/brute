#ifndef SINGLE_H
#define SINGLE_H

#include "config.h"
#include <stdbool.h>

bool password_handler (char *password, void *context);
bool run_single (char *password, config_t *config);

#endif // SINGLE_H
