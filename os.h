#ifndef OS_H
#define OS_H

#define I_AM_HERE printf("Core x is in %s() at %s:%d\n", \
                         __FUNCTION__, __FILE__, __LINE__);

#ifdef __ros__

#include <spinlock.h>
#include <futex.h>
#include <pthread.h>

typedef struct spin_pdr_lock spin_pdr_lock_t;

static inline long futex_wait(int *addr, int val)
{
  return futex(addr, FUTEX_WAIT, val, NULL, NULL, 0);
}

static inline long futex_wake(int *addr, int count)
{
  return futex(addr, FUTEX_WAKE, count, NULL, NULL, 0);
}

static void os_app_init()
{
  pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
  pthread_need_tls(FALSE);
  pthread_lib_init();					/* gives us one vcore */
}

static void os_thread_init()
{
}

#define KWEB_STACK_SZ (PGSIZE * 4)

#elif defined(WITH_PARLIB) /* linux with parlib */
#include <unistd.h>
#include <limits.h>
#include <parlib/spinlock.h>

#define KWEB_STACK_SZ (PTHREAD_STACK_MIN * 4)

#if defined(WITH_UPTHREAD) /* linux with upthread scheduler */
#include <upthread/futex.h>
#include <upthread/upthread.h>

static void os_app_init()
{
  upthread_can_vcore_request(false);
  upthread_can_vcore_steal(true);
  upthread_set_num_vcores(24);
}

static void os_thread_init()
{
}

#define pthread_t upthread_t
#define pthread_attr_t upthread_attr_t
#define pthread_mutex_t upthread_mutex_t
#define pthread_mutexattr_t upthread_mutexattr_t

#define pthread_attr_init upthread_attr_init
#define pthread_mutex_init upthread_mutex_init
#define pthread_attr_setstacksize upthread_attr_setstacksize
#define pthread_attr_setdetachstate upthread_attr_setdetachstate
#define pthread_create upthread_create
#define pthread_yield upthread_yield
#define pthread_mutex_lock upthread_mutex_lock
#define pthread_mutex_unlock upthread_mutex_unlock

#elif defined(WITH_CUSTOM_SCHED) /* linux with custom scheduler */
#define pthread_yield()
#define pthread_create(a,b,c,d) 

static void os_app_init()
{
}

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

static void os_app_init()
{
}

void os_thread_init();

static inline long futex_wait(int *addr, int val)
{
  return syscall(SYS_futex, addr, FUTEX_WAIT, val, NULL);
}

static inline long futex_wake(int *addr, int count)
{
  return syscall(SYS_futex, addr, FUTEX_WAKE, count);
}

#define KWEB_STACK_SZ (PTHREAD_STACK_MIN * 4)

#endif

#endif /* OS_H */
