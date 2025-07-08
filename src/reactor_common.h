#ifndef REACTOR_COMMON_H
#define REACTOR_COMMON_H

#include "common.h"

typedef struct job_t
{
  void *arg;
  status_t (*job_func) (void *);
} job_t;

#endif // REACTOR_COMMON_H
