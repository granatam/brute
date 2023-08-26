#include "multi.h"

#include "queue.h"
#include <assert.h>

bool
run_multi (char *password, config_t *config)
{
  queue_t queue;
  queue_init (&queue);

  (void)password; /* to suppress "unused parameter" warning */
  (void)config;   /* to suppress "unused parameter" warning */
  assert (false && "Not implemented yet");
}
