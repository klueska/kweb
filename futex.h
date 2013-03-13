#ifndef FUTEX_H
#define FUTEX_H

#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

static inline long futex_wait (int *addr, int val)
{
  return syscall(SYS_futex, addr, FUTEX_WAIT, val, NULL);
}

static inline long futex_wake (int *addr, int count)
{
  return syscall (SYS_futex, addr, FUTEX_WAKE, count);
}

#endif
