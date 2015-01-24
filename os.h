#ifndef OS_H
#define OS_H

#define I_AM_HERE printf("Core x is in %s() at %s:%d\n", \
                         __FUNCTION__, __FILE__, __LINE__);

void os_init();
void os_thread_init();

#if defined(__ros__)
#include <spinlock.h>
#include <futex.h>
#include <pthread.h>

#define KWEB_STACK_SZ (PGSIZE * 4)

#if defined(WITH_CUSTOM_SCHED)
#include <ros/bcq.h>
#include "custom_sched.h"
#else
#define mutex_lock(x) pthread_mutex_lock(x)
#define mutex_unlock(x) pthread_mutex_unlock(x)
#define mutex_init(x) pthread_mutex_init(x, 0)
#define mutex_t pthread_mutex_t
#endif

typedef struct spin_pdr_lock spin_pdr_lock_t;

static inline long futex_wait(int *addr, int val)
{
  return futex(addr, FUTEX_WAIT, val, NULL, NULL, 0);
}

static inline long futex_wake(int *addr, int count)
{
  return futex(addr, FUTEX_WAKE, count, NULL, NULL, 0);
}

#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>

#define KWEB_STACK_SZ (PTHREAD_STACK_MIN * 4)

#if defined(WITH_PARLIB) /* linux with parlib */
#include <parlib/spinlock.h>

#if defined(WITH_UPTHREAD) /* linux with upthread scheduler */
#include <upthread/futex.h>
#include <upthread/upthread.h>

#define pthread_t upthread_t
#define pthread_attr_t upthread_attr_t
#define pthread_mutexattr_t upthread_mutexattr_t

#define pthread_attr_init upthread_attr_init
#define pthread_attr_setstacksize upthread_attr_setstacksize
#define pthread_attr_setdetachstate upthread_attr_setdetachstate
#define pthread_create upthread_create
#define pthread_yield upthread_yield

#define mutex_lock(x) upthread_mutex_lock(x)
#define mutex_unlock(x) upthread_mutex_unlock(x)
#define mutex_init(x) upthread_mutex_init(x, 0)
#define mutex_t upthread_mutex_t

#elif defined(WITH_CUSTOM_SCHED) /* linux with custom scheduler */
#include "bcq.h"
#include "custom_sched.h"
#define udelay usleep
#endif

#else /* native linux */

#include "spinlock.h"
#include <linux/futex.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sched.h>
#include <limits.h>
#include <sys/epoll.h>

#define mutex_lock(x) pthread_mutex_lock(x)
#define mutex_unlock(x) pthread_mutex_unlock(x)
#define mutex_init(x) pthread_mutex_init(x, 0)
#define mutex_t pthread_mutex_t

static inline long futex_wait(int *addr, int val)
{
  return syscall(SYS_futex, addr, FUTEX_WAIT, val, NULL);
}

static inline long futex_wake(int *addr, int count)
{
  return syscall(SYS_futex, addr, FUTEX_WAKE, count);
}

#endif
#endif

#endif /* OS_H */
