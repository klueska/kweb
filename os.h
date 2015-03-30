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

/* For all variants except the linux custom-sched. */
#if !defined(WITH_CUSTOM_SCHED)
#define mutex_lock(x) pthread_mutex_lock(x)
#define mutex_unlock(x) pthread_mutex_unlock(x)
#define mutex_init(x) pthread_mutex_init(x, 0)
#define mutex_t pthread_mutex_t
#endif

/* For all variants of linux with parlib */
#if defined(WITH_PARLIB)
#include <parlib/spinlock.h>

/* For just the custom scheduler */
#if defined(WITH_CUSTOM_SCHED)
#include "bcq.h"
#include "custom_sched.h"
#define udelay usleep
#endif

/* For native linux. */
#else

#include "spinlock.h"
#include <linux/futex.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sched.h>
#include <limits.h>
#include <sys/epoll.h>

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
