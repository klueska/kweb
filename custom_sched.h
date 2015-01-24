#ifndef CUSTOM_SCHED_H
#define CUSTOM_SCHED_H

#include <sys/queue.h>

int yield_pcore(int pcoreid);

#ifndef printd
#define printd(...)
#endif

#if defined(__akaros__)

#define PVC_2LS 1
#define TDI 1
#define TDI_YIELDS 1
#define BRUTEX 1
//#define BRUTEX_YIELD_IMMEDIATELY 1
#define ALARM 1
//#define PREFER_UNBLOCKED_SYSC 1
#define MAX_NR_THREADS 50

#else 

#define TDI 1
#define TDI_YIELDS 1
//#define BRUTEX 1
//#define BRUTEX_YIELD_IMMEDIATELY 1
//#define PREFER_UNBLOCKED_SYSC 1
#define MAX_NR_THREADS 50

#endif

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
void wthread_mutex_unlock(mutex_t *m);

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

#endif // CUSTOM_SCHED_H
