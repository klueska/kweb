#include <fcntl.h>
#include <stdio.h>
#include "kweb.h"
#include "tpool.h"
#include "kstats.h"
#include "cpu_util.h"

void os_init(void)
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
	struct epoll_event event;
	c->epollrfd = epoll_create1(0);
	c->epollwfd = epoll_create1(0);
	make_socket_non_blocking(c->socketfd);
	event.data.fd = c->socketfd;
	event.events = EPOLLIN;
	epoll_ctl(c->epollrfd, EPOLL_CTL_ADD, c->socketfd, &event);
	event.data.fd = c->socketfd;
	event.events = EPOLLOUT;
	epoll_ctl(c->epollwfd, EPOLL_CTL_ADD, c->socketfd, &event);
}

void destroy_connection(struct http_connection *c)
{
	close(c->epollrfd);
	close(c->epollwfd);
}

ssize_t timed_read(struct http_connection *c, void *buf, size_t count)
{
  struct epoll_event event;
  tpool_inform_blocking(&tpool);
  epoll_wait(c->epollrfd, &event, 1, KWEB_SREAD_TIMEOUT);
  tpool_inform_unblocked(&tpool);
  return read(c->socketfd, buf, count);
}

ssize_t timed_write(struct http_connection *c, const void *buf, size_t count)
{
  int ret = 0;
  int remaining = count;
  struct epoll_event event;
  while(remaining > 0) {
    tpool_inform_blocking(&tpool);
    epoll_wait(c->epollwfd, &event, 1, KWEB_SWRITE_TIMEOUT);
    tpool_inform_unblocked(&tpool);
    ret = write(c->socketfd, buf, remaining);
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
  init_connection(c);
  enqueue_connection_tail(&kqueue, c);
}
