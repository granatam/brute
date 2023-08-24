#include <assert.h>
#include <pthread.h>
#include <semaphore.h> /* sem_init () is deprecated on MacOS */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PASSWORD_LENGTH (7)
#define QUEUE_SIZE (8)

typedef char password_t[MAX_PASSWORD_LENGTH + 1];

typedef bool (*password_handler_t) (char *password, void *context);

typedef enum
{
  RM_SINGLE,
  RM_MULTI,
} run_mode_t;

typedef enum
{
  BM_ITER,
  BM_RECU,
} brute_mode_t;

typedef struct config_t
{
  run_mode_t run_mode;
  brute_mode_t brute_mode;
  int length;
  char *alph;
  char *hash;
} config_t;

typedef struct task_t
{
  password_t password;
} task_t;

typedef struct queue_t
{
  task_t queue[QUEUE_SIZE];
  int head, tail;
  sem_t full, empty;
  pthread_mutex_t head_mutex, tail_mutex;
} queue_t;

void
queue_init (queue_t *queue)
{
  queue->head = queue->tail = 0;

  sem_init (&queue->full, 0, 0);
  sem_init (&queue->empty, 0, QUEUE_SIZE);

  pthread_mutex_init (&queue->head_mutex, NULL);
  pthread_mutex_init (&queue->tail_mutex, NULL);
}

void
queue_push (queue_t *queue, task_t *task)
{
  sem_wait (&queue->empty);
  pthread_mutex_lock (&queue->tail_mutex);
  queue->queue[queue->tail] = *task;
  queue->tail = (queue->tail + 1) % QUEUE_SIZE;
  pthread_mutex_unlock (&queue->tail_mutex);
  sem_post (&queue->full);
}

void
queue_pop (queue_t *queue, task_t *task)
{
  sem_wait (&queue->full);
  pthread_mutex_lock (&queue->head_mutex);
  *task = queue->queue[queue->head];
  queue->head = (queue->head + 1) % QUEUE_SIZE;
  pthread_mutex_unlock (&queue->head_mutex);
  sem_post (&queue->empty);
}

bool
password_handler (char *password, void *context)
{
  char *hash = (char *)context;
  char *hashed_password = crypt (password, hash);

  return strcmp (hash, hashed_password) == 0;
}

bool
brute_rec (char *password, config_t *config,
           password_handler_t password_handler, void *context, int pos)
{
  if (pos == config->length)
    {
      return password_handler (password, context);
    }
  else
    {
      for (size_t i = 0; config->alph[i] != '\0'; ++i)
        {
          password[pos] = config->alph[i];
          if (brute_rec (password, config, password_handler, context, pos + 1))
            {
              return true;
            }
        }
    }
  return false;
}

bool
brute_rec_wrapper (char *password, config_t *config,
                   password_handler_t password_handler, void *context)
{
  return brute_rec (password, config, password_handler, context, 0);
}

bool
brute_iter (char *password, config_t *config,
            password_handler_t password_handler, void *context)
{
  int alph_size = strlen (config->alph) - 1;
  int idx[config->length];
  memset (idx, 0, config->length * sizeof (int));
  memset (password, config->alph[0], config->length);

  int pos;
  while (true)
    {
      if (password_handler (password, context))
        {
          return true;
        }
      for (pos = config->length - 1; pos >= 0 && idx[pos] == alph_size; --pos)
        {
          idx[pos] = 0;
          password[pos] = config->alph[idx[pos]];
        }
      if (pos < 0)
        {
          return false;
        }
      password[pos] = config->alph[++idx[pos]];
    }
}

bool
run_single (char *password, config_t *config)
{
  switch (config->brute_mode)
    {
    case BM_ITER:
      return brute_iter (password, config, password_handler, config->hash);
    case BM_RECU:
      return brute_rec_wrapper (password, config, password_handler,
                                config->hash);
    }
}

bool
run_multi (char *password, config_t *config)
{
  (void)password; /* to suppress "unused parameter" warning */
  (void)config;   /* to suppress "unused parameter" warning */
  assert (false && "Not implemented yet");
}

int
parse_params (config_t *config, int argc, char *argv[])
{
  int opt = 0;
  while ((opt = getopt (argc, argv, "l:a:h:smir")) != -1)
    {
      switch (opt)
        {
        case 'l':
          config->length = atoi (optarg);
          if (config->length <= 0 || config->length > MAX_PASSWORD_LENGTH)
            {
              printf ("Password's length must be a number between 0 and %d\n",
                      MAX_PASSWORD_LENGTH);
              return -1;
            }
          break;
        case 'a':
          config->alph = optarg;
          break;
        case 'h':
          config->hash = optarg;
          break;
        case 's':
          config->run_mode = RM_SINGLE;
          break;
        case 'm':
          config->run_mode = RM_MULTI;
          break;
        case 'i':
          config->brute_mode = BM_ITER;
          break;
        case 'r':
          config->brute_mode = BM_RECU;
          break;
        default:
          return -1;
        }
    }

  return 0;
}

int
main (int argc, char *argv[])
{
  config_t config = {
    .run_mode = RM_SINGLE,
    .brute_mode = BM_ITER,
    .length = 3,
    .alph = "abc",
    .hash = "abFZSxKKdq5s6", /* crypt ("abc", "abc"); */
  };
  if (parse_params (&config, argc, argv) == -1)
    {
      return EXIT_FAILURE;
    }

  queue_t queue;
  queue_init (&queue);

  char password[config.length + 1];
  password[config.length] = '\0';

  bool is_found;
  switch (config.run_mode)
    {
    case RM_SINGLE:
      is_found = run_single (password, &config);
      break;
    case RM_MULTI:
      is_found = run_multi (password, &config);
      break;
    }

  if (is_found)
    {
      printf ("Password found: %s\n", password);
    }
  else
    {
      printf ("Password not found\n");
    }

  return EXIT_SUCCESS;
}
