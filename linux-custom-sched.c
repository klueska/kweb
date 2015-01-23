#include <fcntl.h>
#include <stdio.h>
#include "kweb.h"
#include "tpool.h"
#include "kstats.h"

void os_init(void)
{
}

int yield_pcore(int pcoreid)
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
}

int tpool_init(struct tpool *t, int size, struct kqueue *q,
               void (*func)(struct kqueue *, struct kitem *), size_t stacksize)
{
  return 0;
}

void tpool_resize(struct tpool *t, int size)
{
}

void tpool_wake(struct tpool *t, int count)
{
}

struct tpool_stats tpool_get_stats(struct tpool *t)
{
  struct tpool_stats s = t->stats;
  return s;
}

void tpool_inform_blocking(struct tpool *t)
{
}

void tpool_inform_unblocked(struct tpool *t)
{
}

int tpool_get_items_processed(struct tpool_stats *prev,
                              struct tpool_stats *curr)
{
  return 0;
}

double tpool_get_average_active_threads(struct tpool_stats *prev,
                                        struct tpool_stats *curr)
{
  return 0;
}

double tpool_get_average_blocked_threads(struct tpool_stats *prev,
                                         struct tpool_stats *curr)
{
  return 0;
}

double tpool_get_average_processing_time(struct tpool_stats *prev,
                                         struct tpool_stats *curr)
{
  return 0;
}

void tpool_print_items_processed(char *prefix,
                                 struct tpool_stats *prev,
                                 struct tpool_stats *curr)
{
}

void tpool_print_average_active_threads(char *prefix,
                                        struct tpool_stats *prev,
                                        struct tpool_stats *curr)
{
}

void tpool_print_average_blocked_threads(char *prefix,
                                        struct tpool_stats *prev,
                                        struct tpool_stats *curr)
{
}

void tpool_print_average_processing_time(char *prefix,
                                         struct tpool_stats *prev,
                                         struct tpool_stats *curr)
{
}

void kstats_init(struct kstats *kstats, struct kqueue *kqueue,
                 struct tpool *tpool, struct cpu_util *cpu_util)
{
}

int kstats_start(struct kstats *kstats, unsigned int period_ms)
{
    return -1;
}

int kstats_stop(struct kstats *kstats)
{
    return -1;
}

void kstats_print_lifetime_statistics(struct kstats *kstats)
{
}

