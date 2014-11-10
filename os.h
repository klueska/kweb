#ifndef OS_H
#define OS_H

#ifdef __ros__

#include <spinlock.h>
#include <futex.h>

#define I_AM_HERE printf("Core %d is in %s() at %s:%d\n", vcore_id(), \
                         __FUNCTION__, __FILE__, __LINE__);


//#define PVC_2LS 1
#define TDI 1
#define TDI_YIELDS 1
#define BRUTEX 1
//#define BRUTEX_YIELD_IMMEDIATELY 1
#define ALARM 1
//#define PREFER_UNBLOCKED_SYSC 1
#define MAX_NR_THREADS 50

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


typedef struct {
	struct wthread_queue		waiters;
	struct spin_pdr_lock		lock;
	bool						in_use;
} mutex_t;

void wthread_mutex_init(mutex_t *m);
void wthread_mutex_lock(mutex_t *m);
void wthread_mutex_unlock(mutex_t *m);


typedef struct {
	atomic_t					lock;
} brutex_t;

void wthread_brutex_init(brutex_t *m);
void wthread_brutex_lock(brutex_t *m);
void wthread_brutex_unlock(brutex_t *m);


//#define mutex_lock(x) wthread_rutex_lock(x)
//#define mutex_unlock(x) wthread_rutex_unlock(x)
//#define mutex_init(x) wthread_rutex_init(x);
//#define mutex_t rutex_t

// XXX G
#ifdef BRUTEX

#define mutex_lock(x) wthread_brutex_lock(x)
#define mutex_unlock(x) wthread_brutex_unlock(x)
#define mutex_init(x) wthread_brutex_init(x);
#define mutex_t brutex_t

#else

#define mutex_lock(x) wthread_mutex_lock(x)
#define mutex_unlock(x) wthread_mutex_unlock(x)
#define mutex_init(x) wthread_mutex_init(x);

#endif

#if 0
#define spinlock_t struct spin_pdr_lock
#define spinlock_init(x) spin_pdr_init(x)
#define spinlock_lock(x) spin_pdr_lock(x)
#define spinlock_unlock(x) spin_pdr_unlock(x)
#endif

#define KWEB_STACK_SZ (PGSIZE * 4)

int yield_pcore(int pcoreid);

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
