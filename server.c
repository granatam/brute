#include "server.h"

#include "brute.h"
#include "common.h"
#include "multi.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// TODO: Add status checks and cleanup in case of errors
// TODO: Check if code could be more readable (e.g. rewrite
// context->context->context type code)
// TODO: Change namings for context variables? Maybe something like mt_ctx,
// cl_ctx, serv_ctx, local_ctx? Also it could work in other files and improve
// project readability maybe, I'm not sure.

static status_t
serv_context_init (serv_context_t *context, config_t *config)
{
  if (mt_context_init ((mt_context_t *)context, config) == S_FAILURE)
    return (S_FAILURE);

  return (S_SUCCESS);
}

static status_t
serv_context_destroy (serv_context_t *context)
{
  if (mt_context_destroy ((mt_context_t *)context) == S_FAILURE)
    return (S_FAILURE);

  return (S_SUCCESS);
}

static status_t
delegate_task (int socket_fd, task_t *task, password_t password)
{
  if (send_wrapper (socket_fd, task, sizeof (task_t), 0) == S_FAILURE)
    {
      print_error ("Could not send data to client\n");
      return (S_FAILURE);
    }

  int size;
  if (recv_wrapper (socket_fd, &size, sizeof (int), 0) == S_FAILURE)
    {
      print_error ("Could not receive data from client\n");
      return (S_FAILURE);
    }

  if (size != 0)
    {
      if (recv_wrapper (socket_fd, password, sizeof (password_t), 0)
          == S_FAILURE)
        {
          print_error ("Could not receive data from client\n");
          return (S_FAILURE);
        }
    }

  return (S_SUCCESS);
}

static void *
handle_client (void *arg)
{
  cl_context_t *cl_context = (cl_context_t *)arg;
  cl_context_t local_context = *cl_context;
  serv_context_t *context = local_context.context;

  if (pthread_mutex_unlock (&cl_context->context->context.mutex) != 0)
    {
      print_error ("Could not unlock mutex\n");
      return (NULL);
    }

  if (send_wrapper (local_context.socket_fd, context->context.config->hash,
                    HASH_LENGTH, 0)
      == S_FAILURE)
    {
      print_error ("Could not send hash to client\n");
      return (NULL);
    }

  while (true)
    {
      task_t task;
      if (queue_pop (&context->context.queue, &task) == S_FAILURE)
        return (NULL);

      task.to = task.from;
      task.from = 0;

      if (delegate_task (local_context.socket_fd, &task,
                         context->context.password)
          == S_FAILURE)
        {
          if (queue_push (&context->context.queue, &task) == S_FAILURE)
            print_error ("Could not push to the queue\n");
          return (NULL);
        }

      if (pthread_mutex_lock (&context->context.mutex) != 0)
        {
          print_error ("Could not lock a mutex\n");
          return (NULL);
        }
      pthread_cleanup_push (cleanup_mutex_unlock, &context->context.mutex);

      --context->context.passwords_remaining;
      if (context->context.passwords_remaining == 0
          || context->context.password[0] != 0)
        if (pthread_cond_signal (&context->context.cond_sem) != 0)
          {
            print_error ("Could not signal a condition\n");
            return (NULL);
          }

      pthread_cleanup_pop (!0);
    }

  close (context->socket_fd);

  return (NULL);
}

static void *
handle_clients (void *arg)
{
  serv_context_t *context = (serv_context_t *)arg;

  while (true)
    {
      int client_socket = accept (context->socket_fd, NULL, NULL);
      if (client_socket == -1)
        {
          print_error ("Could not accept new connection\n");
          continue;
        }

      cl_context_t client_context = {
        .context = context,
        .socket_fd = client_socket,
      };

      if (thread_create (&context->context.thread_pool, handle_client,
                         &client_context)
          == S_FAILURE)
        {
          print_error ("Could not create client thread\n");
          if (pthread_mutex_unlock (&client_context.context->context.mutex)
              != 0)
            {
              print_error ("Could not lock mutex\n");
              return (NULL);
            }
          continue;
        }

      if (pthread_mutex_lock (&client_context.context->context.mutex) != 0)
        {
          print_error ("Could not lock mutex\n");
          return (NULL);
        }
    }

  return (NULL);
}

bool
run_server (task_t *task, config_t *config)
{
  serv_context_t context;

  if (serv_context_init (&context, config) == S_FAILURE)
    {
      print_error ("Could not initialize server context\n");
      return (false);
    }

  if ((context.socket_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      print_error ("Could not initialize server socket\n");
      goto fail;
    }

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr (config->addr);
  serv_addr.sin_port = htons (config->port);

  if (bind (context.socket_fd, (struct sockaddr *)&serv_addr,
            sizeof (serv_addr))
      == -1)
    {
      print_error ("Could not bind socket\n");
      goto fail;
    }

  if (listen (context.socket_fd, 10) == -1)
    {
      print_error ("Could not start listening connections\n");
      goto fail;
    }

  pthread_t clients_thread;
  if (pthread_create (&clients_thread, NULL, handle_clients, &context) != 0)
    {
      print_error ("Could not create clients thread\n");
      goto fail;
    }

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  mt_context_t *mt_context = (mt_context_t *)&context;

  brute (task, config, queue_push_wrapper, mt_context);

  if (pthread_mutex_lock (&mt_context->mutex) != 0)
    {
      print_error ("Could not lock a mutex\n");
      goto fail;
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &mt_context->mutex);

  while (mt_context->passwords_remaining != 0 && mt_context->password[0] == 0)
    {
      if (pthread_cond_wait (&mt_context->cond_sem, &mt_context->mutex) != 0)
        {
          print_error ("Could not wait on a condition\n");
          goto fail;
        }
    }

  pthread_cleanup_pop (!0);

  if (queue_cancel (&mt_context->queue) == S_FAILURE)
    {
      print_error ("Could not cancel a queue\n");
      goto fail;
    }

  pthread_cancel (clients_thread);
  pthread_join (clients_thread, NULL);

  close (context.socket_fd);

  if (mt_context->password[0] != 0)
    memcpy (task->password, mt_context->password,
            sizeof (mt_context->password));

  if (serv_context_destroy (&context) == S_FAILURE)
    {
      print_error ("Could not destroy server context\n");
      return (false);
    }

  return (mt_context->password[0] != 0);

fail:
  if (serv_context_destroy (&context) == S_FAILURE)
    print_error ("Could not destroy server context\n");
  return (false);
}
