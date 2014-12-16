#include <fcntl.h>
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
