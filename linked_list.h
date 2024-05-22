#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include "common.h"

#include <pthread.h>

typedef struct ll_node_t
{
  struct ll_node_t *prev;
  struct ll_node_t *next;
  char payload[0];
} ll_node_t;

typedef struct linked_list_t
{
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int count;
  bool cancelled;
  ll_node_t nodes;
  size_t unit_size;
} linked_list_t;

status_t linked_list_init (linked_list_t *list, size_t unit_size);
status_t linked_list_push (linked_list_t *list, void *info);
status_t linked_list_pop (linked_list_t *list, void **payload);
status_t linked_list_destroy (linked_list_t *list);

#endif /* LINKED_LIST_H */
