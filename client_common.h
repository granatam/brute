#ifndef CLIENT_COMMON_H
#define CLIENT_COMMON_H

#include "common.h"

#include <stdbool.h>

status_t handle_alph (int socket_fd, char *alph);
status_t handle_hash (int socket_fd, char *hash);
int ms_sleep (long milliseconds);

#endif // CLIENT_COMMON_H
