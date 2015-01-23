#ifndef OS_H
#define OS_H

#define I_AM_HERE printf("Core x is in %s() at %s:%d\n", \
                         __FUNCTION__, __FILE__, __LINE__);

void os_init();
void os_thread_init();
int yield_pcore(int pcoreid);

#if defined(__ros__)
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

#define KWEB_STACK_SZ (PGSIZE * 4)

#if defined(WITH_CUSTOM_SCHED)

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
    struct wthread_queue        waiters;
    bool                        in_use;
} rutex_t;

void wthread_rutex_init(rutex_t *m);
void wthread_rutex_lock(rutex_t *m);
void wthread_rutex_unlock(rutex_t *m);


typedef struct {
    struct wthread_queue        waiters;
    struct spin_pdr_lock        lock;
    bool                        in_use;
} mutex_t;

void wthread_mutex_init(mutex_t *m);
void wthread_mutex_lock(mutex_t *m);

typedef struct {
    atomic_t                    lock;
} brutex_t;

void wthread_brutex_init(brutex_t *m);
void wthread_brutex_lock(brutex_t *m);
void wthread_brutex_unlock(brutex_t *m);

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

#else  // native akaros

#define mutex_lock(x) pthread_mutex_lock(x)
#define mutex_unlock(x) pthread_mutex_unlock(x)
#define mutex_init(x) pthread_mutex_init(x, 0)
#define mutex_t pthread_mutex_t

#endif

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

#define pthread_yield()
#define pthread_create(a,b,c,d) 

#define mutex_lock(x)
#define mutex_unlock(x) 
#define mutex_init(x)
#define mutex_t int

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

#endif
#endif

#endif /* OS_H */
