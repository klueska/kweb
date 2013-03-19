#include <linux/futex.h>
#include <pthread.h>
#include <limits.h>
#include "futex.h"
#include "tpool.h"

static void *__thread_wrapper(void *arg)
{
  int total_enqueued = 0;
  struct tpool *t = (struct tpool*)arg;

  spinlock_lock(&t->lock);
  t->active_threads++;
  spinlock_unlock(&t->lock);

  while(1) {
    spinlock_lock(&t->lock);
    t->active_threads_sum += t->active_threads;
    t->active_threads_samples++;
    struct request *r = request_queue_dequeue_request(t->q);
    if(r == NULL) {
      t->active_threads--;
      total_enqueued = t->q->total_enqueued;
    }
    spinlock_unlock(&t->lock);

    if(r) {
     t->func(t->q, r);
    }
    else {
      futex_wait(&t->q->total_enqueued, total_enqueued);
      spinlock_lock(&t->lock);
      t->active_threads++;
      spinlock_unlock(&t->lock);
    }
  }
}

int tpool_init(struct tpool *t, int size, struct request_queue *q,
               void (*func)(struct request_queue *, struct request *))
{
  t->q = q;
  t->size = 0;
  t->func = func;
  t->active_threads = 0;
  t->active_threads_sum = 0;
  t->active_threads_samples = 0;

  spinlock_init(&t->lock);

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  for(int i=0; i<size; i++) {
    if(pthread_create(&thread, &attr, __thread_wrapper, t) == 0)
      t->size++;
    else
      break;
  }
  return t->size;
}

void tpool_wake(struct tpool *t, int count)
{
  futex_wake(&t->q->total_enqueued, count);
}

