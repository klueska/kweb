/*
 * Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Lesser GNU General Public License for more details.
 * 
 * See COPYING.LESSER for details on the GNU Lesser General Public License.
 * See COPYING for details on the GNU General Public License.
 */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

static __inline void
cpu_relax(void)
{
	asm volatile("pause" : : : "memory");
}

#define SPINLOCK_INITIALIZER {0}

typedef struct spinlock {
  int lock;
} spinlock_t;

static void spinlock_init(spinlock_t *lock)
{
  lock->lock = 0;
}

static int spinlock_trylock(spinlock_t *lock)
{
  return __sync_lock_test_and_set(&lock->lock, EBUSY);
}

static void spinlock_lock(spinlock_t *lock)
{
  while (spinlock_trylock(lock))
    cpu_relax();
}

static inline void spinlock_unlock(spinlock_t *lock)
{
  __sync_lock_release(&lock->lock, 0);
}

#define spin_pdr_lock_t spinlock_t
#define spin_pdr_init spinlock_init
#define spin_pdr_lock spinlock_lock
#define spin_pdr_unlock spinlock_unlock

#ifdef __cplusplus
}
#endif

#endif // SPINLOCK_H
