#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <stdio.h>
#include "kweb.h"
#include "tpool.h"

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

static void pin_to_core(int core)
{
  cpu_set_t c;
  CPU_ZERO(&c);
  CPU_SET(core, &c);
  sched_setaffinity(0, sizeof(cpu_set_t), &c);
  sched_yield();
}

void os_thread_init()
{
//  static int next_core = 0;
//  int n = __sync_fetch_and_add(&next_core, 1);
//  printf("next_core: %d\n", n % get_nprocs());
//  pin_to_core(n % get_nprocs());
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
