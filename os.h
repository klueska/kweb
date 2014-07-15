#ifndef OS_H
#define OS_H

#define I_AM_HERE printf("Core x is in %s() at %s:%d\n", \
                         __FUNCTION__, __FILE__, __LINE__);

#ifdef __ros__

#include <spinlock.h>
#include <futex.h>

static inline long futex_wait(int *addr, int val)
{
  return futex(addr, FUTEX_WAIT, val, NULL, NULL, 0);
}

static inline long futex_wake(int *addr, int count)
{
  return futex(addr, FUTEX_WAKE, count, NULL, NULL, 0);
}

#define KWEB_STACK_SZ (PGSIZE * 4)

#else /* linux */

#include "spinlock.h"
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>
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

#define KWEB_STACK_SZ (PTHREAD_STACK_MIN * 4)

#endif

#endif /* OS_H */
