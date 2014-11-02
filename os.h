#ifndef OS_H
#define OS_H

#ifdef __ros__

#include <spinlock.h>
#include <futex.h>

#define I_AM_HERE printf("Core %d is in %s() at %s:%d\n", vcore_id(), \
                         __FUNCTION__, __FILE__, __LINE__);

/* Need this here since the connection struct includes a mutex_t */
struct wthread;
TAILQ_HEAD(wthread_queue, wthread);

typedef struct {
	struct wthread_queue		waiters;
	bool						in_use;
} rutex_t;

void wthread_rutex_init(rutex_t *m);
void wthread_rutex_lock(rutex_t *m);
void wthread_rutex_unlock(rutex_t *m);

#define mutex_lock(x) wthread_rutex_lock(x)
#define mutex_unlock(x) wthread_rutex_unlock(x)
#define mutex_init(x) wthread_rutex_init(x);
#define mutex_t rutex_t

#define KWEB_STACK_SZ (PGSIZE * 4)

#else /* linux */

#include "spinlock.h"
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <limits.h>
#include <sys/epoll.h>

#define I_AM_HERE printf("Core x is in %s() at %s:%d\n", \
                         __FUNCTION__, __FILE__, __LINE__);

static inline long futex_wait(int *addr, int val)
{
  return syscall(SYS_futex, addr, FUTEX_WAIT, val, NULL);
}

static inline long futex_wake(int *addr, int count)
{
  return syscall(SYS_futex, addr, FUTEX_WAKE, count);
}

#define mutex_lock(x) pthread_mutex_lock(x)
#define mutex_unlock(x) pthread_mutex_unlock(x)
#define mutex_init(x) pthread_mutex_init(x, 0)
#define mutex_t pthread_mutex_t

#define KWEB_STACK_SZ (PTHREAD_STACK_MIN * 4)

#endif

#endif /* OS_H */
