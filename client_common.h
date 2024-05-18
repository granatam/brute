#ifndef CLIENT_COMMON_H
#define CLIENT_COMMON_H

#include "common.h"
#include "config.h"
#include "single.h"

#include <stdbool.h>

status_t handle_alph (int socket_fd, char *alph);
status_t handle_hash (int socket_fd, char *hash);

#endif // CLIENT_COMMON_H
