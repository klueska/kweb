#include <parlib.h>
#include <unistd.h>
#include <alarm.h>
#include "kweb.h"
#include "tpool.h"

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

ssize_t timed_write(struct http_connection *c, const void *buf, size_t count)
{
	ssize_t ret;
	struct alarm_waiter waiter;

	init_awaiter(&waiter, alarm_abort_sysc);
	waiter.data = current_uthread;

	set_awaiter_rel(&waiter, KWEB_SREAD_TIMEOUT * 1000);
	set_alarm(&waiter);

	tpool_inform_blocking(&tpool);
	ret = write(c->socketfd, buf, count);
	tpool_inform_unblocked(&tpool);

	unset_alarm(&waiter);
	return ret;
}
