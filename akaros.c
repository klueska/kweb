#include <parlib.h>
#include <unistd.h>
#include <alarm.h>
#include "kweb.h"
#include "tpool.h"
#include "kstats.h"
#include "cpu_util.h"

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

  pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
  pthread_need_tls(FALSE);
  pthread_lib_init();					/* gives us one vcore */
}

void os_thread_init()
{
}

int usleep(useconds_t usec)
{
	sys_block(usec);
	/* TODO: should check for syscall abortion and error out.  man usleep */
	return 0;
}

void init_connection(struct http_connection *c)
{
}

void destroy_connection(struct http_connection *c)
{
}

ssize_t timed_read(struct http_connection *c, void *buf, size_t count)
{
	ssize_t ret;
	struct alarm_waiter waiter;

	init_awaiter(&waiter, alarm_abort_sysc);
	waiter.data = current_uthread;
	set_awaiter_rel(&waiter, KWEB_SREAD_TIMEOUT * 1000);
	set_alarm(&waiter);

	tpool_inform_blocking(&tpool);
	ret = read(c->socketfd, buf, count);
	tpool_inform_unblocked(&tpool);

	unset_alarm(&waiter);
	return ret;
}

ssize_t timed_write(struct http_connection *c, const char *buf, size_t count)
{
	ssize_t ret = 0;
	int remaining = count;
	struct alarm_waiter waiter;

	while(remaining > 0) {
		init_awaiter(&waiter, alarm_abort_sysc);
		waiter.data = current_uthread;
		set_awaiter_rel(&waiter, KWEB_SREAD_TIMEOUT * 1000);
		set_alarm(&waiter);

		tpool_inform_blocking(&tpool);
		ret = write(c->socketfd, &buf[count-remaining], remaining);
		tpool_inform_unblocked(&tpool);
		unset_alarm(&waiter);
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
