#include <fcntl.h>
#include <stdio.h>
#include "kweb.h"
#include "tpool.h"
#include "kstats.h"
#include "cpu_util.h"
#include "os.h"

void os_init()
{
  extern struct tpool tpool;
  extern struct kqueue kqueue;
  extern struct kstats kstats;
  extern struct cpu_util cpu_util;
  extern struct server_stats server_stats;
  extern int tpool_size;

  kqueue_init(&kqueue, sizeof(struct http_connection));
  tpool_init(&tpool, tpool_size, &kqueue, http_server, KWEB_STACK_SZ);
  cpu_util_init(&cpu_util);
  kstats_init(&kstats, &kqueue, &tpool, &cpu_util);

  upthread_can_vcore_request(false);
  upthread_can_vcore_steal(true);
  upthread_set_num_vcores(32);
}

void os_thread_init()
{
}

static int make_socket_non_blocking(int sfd)
{
  int flags, s;
  flags = fcntl (sfd, F_GETFL, 0);
  if(flags == -1) {
    perror("fcntl");
    return -1;
  }
  flags |= O_NONBLOCK;
  s = fcntl (sfd, F_SETFL, flags);
  if(s == -1) {
    perror("fcntl");
    return -1;
  }
  return 0;
}

void init_connection(struct http_connection *c)
{
	make_socket_non_blocking(c->socketfd);
}

void destroy_connection(struct http_connection *c)
{
}

ssize_t timed_read(struct http_connection *c, void *buf, size_t count)
{
  set_syscall_timeout(KWEB_SREAD_TIMEOUT * 1000);
  tpool_inform_blocking(&tpool);
  int ret = read(c->socketfd, buf, count);
  tpool_inform_unblocked(&tpool);
  return ret;
}

ssize_t timed_write(struct http_connection *c, const void *buf, size_t count)
{
  int ret = 0;
  int remaining = count;
  while(remaining > 0) {
    tpool_inform_blocking(&tpool);
    ret = write(c->socketfd, buf, remaining);
    tpool_inform_unblocked(&tpool);
    if(ret < 0)
      return ret;
    remaining -= ret;
  }
  return count;
}

void dispatch_call(int call_fd, void *client_addr)
{
  extern struct kqueue kqueue; /* global, kweb.c */
  struct http_connection *c;

  c = kqueue_create_item(&kqueue);
  c->burst_length = MAX_BURST;
  c->ref_count = 0;
  c->socketfd = call_fd;
  c->buf_length = 0;
  mutex_init(&c->writelock);
  c->should_close = 0;
  init_connection(c);
  enqueue_connection_tail(&kqueue, c);
}
