#include "server.h"

#include "brute.h"
#include "common.h"
#include "multi.h"

#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void *
handle_client (void *arg)
{
  cl_context_t *cl_context = (cl_context_t *)arg;
  serv_context_t *context = cl_context->context;

  while (true)
    {
      task_t task;
      if (queue_pop (&context->context.queue, &task) == S_FAILURE)
        return (NULL);

      task.to = task.from;
      task.from = 0;

      if (send (cl_context->socket_fd, &task, sizeof (task), 0) == -1)
        {
          print_error ("Could not send data to client\n");
          return (NULL);
        }

      int size;
      if (recv (cl_context->socket_fd, &size, sizeof (int), 0) == -1)
        {
          print_error ("Could not receive data from client\n");
          return (NULL);
        }

      if (size != 0)
        {
          if (recv (cl_context->socket_fd, task.password,
                    sizeof (task.password), 0)
              == -1)
            {
              print_error ("Could not receive data from client\n");
              return (NULL);
            }

          memcpy (context->context.password, task.password,
                  sizeof (task.password));
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

void *
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

      pthread_t client_thread;
      if (pthread_create (&client_thread, NULL, handle_client, &client_context)
          != 0)
        {
          print_error ("Could not create client thread\n");
          continue;
        }

      pthread_join (client_thread, NULL);
    }

  return (NULL);
}

bool
run_server (task_t *task, config_t *config)
{
  serv_context_t context;
  mt_context_t *mt_context = (mt_context_t *)&context;

  if (mt_context_init (mt_context, config) == S_FAILURE)
    return (false);

  if ((context.socket_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      print_error ("Could not initialize server socket\n");
      return (false);
    }

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons (9000);

  if (bind (context.socket_fd, (struct sockaddr *)&serv_addr,
            sizeof (serv_addr))
      == -1)
    {
      print_error ("Could not bind socket\n");
      return (false);
    }

  if (listen (context.socket_fd, 10) == -1)
    {
      print_error ("Could not start listening connections\n");
      return (false);
    }

  pthread_t clients_thread;
  if (pthread_create (&clients_thread, NULL, handle_clients, &context) != 0)
    {
      print_error ("Could not create clients thread\n");
      return (false);
    }

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  brute (task, config, queue_push_wrapper, mt_context);

  if (pthread_mutex_lock (&mt_context->mutex) != 0)
    {
      print_error ("Could not lock a mutex\n");
      return (false);
    }
  pthread_cleanup_push (cleanup_mutex_unlock, &mt_context->mutex);

  while (mt_context->passwords_remaining != 0 && mt_context->password[0] == 0)
    {
      if (pthread_cond_wait (&mt_context->cond_sem, &mt_context->mutex) != 0)
        {
          print_error ("Could not wait on a condition\n");
          return (false);
        }
    }

  pthread_cleanup_pop (!0);

  if (queue_cancel (&mt_context->queue) == S_FAILURE)
    {
      print_error ("Could not cancel a queue\n");
      return (false);
    }

  pthread_cancel (clients_thread);
  pthread_join (clients_thread, NULL);

  close (context.socket_fd);

  if (mt_context->password[0] != 0)
    memcpy (task->password, mt_context->password,
            sizeof (mt_context->password));

  if (mt_context_destroy (mt_context) == S_FAILURE)
    return (false);

  return (mt_context->password[0] != 0);
}
