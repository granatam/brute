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

static status_t
socket_array_init (socket_array_t *arr)
{
  arr->size = 0;
  arr->capacity = 2;
  arr->data = calloc (arr->capacity, sizeof (int));
  if (!arr->data)
    {
      print_error ("Could not allocate memory for socket array\n");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

static status_t
socket_array_add (socket_array_t *arr, int socket_fd)
{
  arr->data[arr->size++] = socket_fd;
  if (arr->size == arr->capacity)
    {
      arr->capacity *= 2;
      int *tmp = realloc (arr->data, arr->capacity);
      if (!tmp)
        {
          print_error ("Could not reallocate memory for socket array\n");
          return (S_FAILURE);
        }
      arr->data = tmp;
    }

  return (S_SUCCESS);
}

static status_t
close_client (int socket_fd)
{
  command_t cmd = CMD_EXIT;
  if (send_wrapper (socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
    {
      print_error ("Could not send CMD_EXIT to client\n");
      return (S_FAILURE);
    }

  shutdown (socket_fd, SHUT_RDWR);
  close (socket_fd);
  // TODO: Remove debug output
  print_error ("After close client %d\n", socket_fd);

  return (S_SUCCESS);
}

static status_t
socket_array_close_all (socket_array_t *arr)
{
  for (size_t i = 0; i < arr->size; ++i)
    close_client (arr->data[i]);

  return (S_SUCCESS);
}

static status_t
serv_context_init (serv_context_t *context, config_t *config)
{
  if (mt_context_init ((mt_context_t *)context, config) == S_FAILURE)
    return (S_FAILURE);

  if ((context->socket_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      print_error ("Could not initialize server socket\n");
      return (S_FAILURE);
    }

  socket_array_init (&context->sock_arr);

  int option = 1;
  setsockopt (context->socket_fd, SOL_SOCKET, SO_REUSEADDR, &option,
              sizeof (option));

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr (config->addr);
  serv_addr.sin_port = htons (config->port);

  if (bind (context->socket_fd, (struct sockaddr *)&serv_addr,
            sizeof (serv_addr))
      == -1)
    {
      print_error ("Could not bind socket\n");
      goto fail;
    }

  if (listen (context->socket_fd, 10) == -1)
    {
      print_error ("Could not start listening connections\n");
      goto fail;
    }

  return (S_SUCCESS);

fail:
  close (context->socket_fd);
  return (S_FAILURE);
}

static status_t
serv_context_destroy (serv_context_t *context)
{
  if (mt_context_destroy ((mt_context_t *)context) == S_FAILURE)
    return (S_FAILURE);

  socket_array_close_all (&context->sock_arr);

  free (context->sock_arr.data);

  shutdown (context->socket_fd, SHUT_RDWR);
  if (close (context->socket_fd) != 0)
    {
      print_error ("Could not close server socket\n");
      return (S_FAILURE);
    }

  return (S_SUCCESS);
}

static status_t
send_hash (cl_context_t *cl_ctx, mt_context_t *mt_ctx)
{
  command_t cmd = CMD_HASH;
  if (send_wrapper (cl_ctx->socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
    {
      print_error ("Could not send CMD_HASH to client\n");
      return (S_FAILURE);
    }

  if (send_wrapper (cl_ctx->socket_fd, mt_ctx->config->hash, HASH_LENGTH, 0)
      == S_FAILURE)
    {
      print_error ("Could not send hash to client\n");
      return (S_FAILURE);
    }

  print_error ("Sent hash %s to client\n", mt_ctx->config->hash);

  return (S_SUCCESS);
}

static status_t
send_alph (cl_context_t *cl_ctx, mt_context_t *mt_ctx)
{
  command_t cmd = CMD_ALPH;
  if (send_wrapper (cl_ctx->socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
    {
      print_error ("Could not send CMD_ALPH to client\n");
      return (S_FAILURE);
    }

  int32_t length = strlen (mt_ctx->config->alph);
  if (send_wrapper (cl_ctx->socket_fd, &length, sizeof (length), 0)
      == S_FAILURE)
    {
      print_error ("Could not send alphabet length to client\n");
      return (S_FAILURE);
    }

  if (send_wrapper (cl_ctx->socket_fd, mt_ctx->config->alph, length, 0)
      == S_FAILURE)
    {
      print_error ("Could not send alphabet to client\n");
      return (S_FAILURE);
    }

  print_error ("Sent alph %s to client %d\n", mt_ctx->config->alph,
               cl_ctx->socket_fd);

  return (S_SUCCESS);
}

static status_t
delegate_task (int socket_fd, task_t *task, mt_context_t *ctx)
{
  command_t cmd = CMD_TASK;

  if (send_wrapper (socket_fd, &cmd, sizeof (cmd), 0) == S_FAILURE)
    {
      print_error ("Could not send CMD_TASK to client\n");
      return (S_FAILURE);
    }

  if (send_wrapper (socket_fd, task, sizeof (*task), 0) == S_FAILURE)
    {
      print_error ("Could not send task to client\n");
      return (S_FAILURE);
    }

  // TODO: Remove debug output
  print_error ("Sent task %s to client %d\n", task->password, socket_fd);

  int32_t size;
  if (recv_wrapper (socket_fd, &size, sizeof (size), 0) == S_FAILURE)
    {
      print_error ("Could not receive password size from client\n");
      return (S_FAILURE);
    }

  // TODO: Remove debug output
  // print_error ("Received %d from client\n", size);

  if (size != 0)
    {
      if (recv_wrapper (socket_fd, ctx->password, sizeof (password_t), 0)
          == S_FAILURE)
        {
          print_error ("Could not receive password from client\n");
          return (S_FAILURE);
        }

      if (queue_cancel (&ctx->queue) == S_FAILURE)
        {
          print_error ("Could not cancel a queue\n");
          return (S_FAILURE);
        }

      // TODO: Remove debug output
      print_error ("Received password %s from client %d\n", ctx->password,
                   socket_fd);
    }

  return (S_SUCCESS);
}

static void *
handle_client (void *arg)
{
  cl_context_t *cl_ctx = (cl_context_t *)arg;
  cl_context_t local_ctx = *cl_ctx;
  mt_context_t *mt_ctx = &local_ctx.context->context;

  if (pthread_mutex_unlock (&cl_ctx->mutex) != 0)
    {
      print_error ("Could not unlock mutex\n");
      goto end;
    }

  if (send_hash (&local_ctx, mt_ctx) == S_FAILURE)
    goto end;

  if (send_alph (&local_ctx, mt_ctx) == S_FAILURE)
    goto end;

  while (true)
    {
      task_t task;
      // TODO: != QS_SUCCESS or == QS_FAILURE?
      if (queue_pop (&mt_ctx->queue, &task) != QS_SUCCESS)
        goto end;

      task.to = task.from;
      task.from = 0;

      if (delegate_task (local_ctx.socket_fd, &task, mt_ctx) == S_FAILURE)
        {
          if (queue_push (&mt_ctx->queue, &task) == QS_FAILURE)
            print_error ("Could not push to the queue\n");

          goto end;
        }

      if (pthread_mutex_lock (&mt_ctx->mutex) != 0)
        {
          print_error ("Could not lock a mutex\n");
          return (NULL);
        }

      // TODO: Remove debug output
      if (mt_ctx->password[0] != 0)
        print_error ("After delegate task and mutex lock\n");

      // TODO: Could be moved into separate function in multi.c now
      if (--mt_ctx->passwords_remaining == 0 || mt_ctx->password[0] != 0)
        {
          // close_client (local_ctx.socket_fd);
          if (pthread_cond_signal (&mt_ctx->cond_sem) != 0)
            {
              print_error ("Could not signal a condition\n");
              return (NULL);
            }
          // TODO: Remove debug output
          print_error ("After signal\n");
          pthread_mutex_unlock (&mt_ctx->mutex);
          return (NULL);
        }
      pthread_mutex_unlock (&mt_ctx->mutex);
    }

end:
  // close_client (local_ctx.socket_fd);
  return (NULL);
}

static void *
handle_clients (void *arg)
{
  serv_context_t *serv_ctx = (serv_context_t *)arg;
  mt_context_t *mt_ctx = &serv_ctx->context;

  cl_context_t cl_ctx = {
    .context = serv_ctx,
  };

  if (pthread_mutex_init (&cl_ctx.mutex, NULL) != 0)
    {
      print_error ("Could not create mutex\n");
      return (NULL);
    }

  if (pthread_mutex_lock (&cl_ctx.mutex) != 0)
    {
      print_error ("Could not lock mutex\n");
      return (NULL);
    }

  while (true)
    {
      if ((cl_ctx.socket_fd = accept (serv_ctx->socket_fd, NULL, NULL)) == -1)
        {
          print_error ("Could not accept new connection\n");
          continue;
        }

      // TODO: Remove debug output
      print_error ("Accepted new connection\n");

      socket_array_add (&serv_ctx->sock_arr, cl_ctx.socket_fd);

      if (thread_create (&mt_ctx->thread_pool, handle_client, &cl_ctx)
          == S_FAILURE)
        {
          print_error ("Could not create client thread\n");

          close_client (cl_ctx.socket_fd);
          continue;
        }

      // TODO: Remove debug output
      print_error ("Created new client thread\n");

      if (pthread_mutex_lock (&cl_ctx.mutex) != 0)
        {
          print_error ("Could not lock mutex\n");
          close (cl_ctx.socket_fd);
          continue;
        }

      // TODO: Remove debug output
      // print_error ("Mutex locked\n");
    }

  return (NULL);
}

bool
run_server (task_t *task, config_t *config)
{
  serv_context_t context;

  // TODO: Remove debug output
  print_error ("Starting server\n");
  if (serv_context_init (&context, config) == S_FAILURE)
    {
      print_error ("Could not initialize server context\n");
      return (false);
    }

  if (thread_create (&context.context.thread_pool, handle_clients, &context)
      == S_FAILURE)
    {
      print_error ("Could not create clients thread\n");
      goto fail;
    }

  task->from = (config->length < 3) ? 1 : 2;
  task->to = config->length;

  mt_context_t *mt_ctx = (mt_context_t *)&context;

  brute (task, config, queue_push_wrapper, mt_ctx);

  if (wait_password (mt_ctx) == S_FAILURE)
    goto fail;

  if (queue_cancel (&mt_ctx->queue) == S_FAILURE)
    {
      print_error ("Could not cancel a queue\n");
      goto fail;
    }

  if (mt_ctx->password[0] != 0)
    memcpy (task->password, mt_ctx->password, sizeof (mt_ctx->password));

  if (serv_context_destroy (&context) == S_FAILURE)
    {
      print_error ("Could not destroy server context\n");
      // return (false);
    }

  // TODO: Remove debug output
  print_error ("After serv_context_destroy\n");

  return (mt_ctx->password[0] != 0);

fail:
  if (serv_context_destroy (&context) == S_FAILURE)
    print_error ("Could not destroy server context\n");

  return (false);
}
