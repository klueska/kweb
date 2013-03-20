#include <stdio.h>
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
      total_enqueued = t->q->qstats.total_enqueued;
    }
    spinlock_unlock(&t->lock);

    if(r) {
     t->func(t->q, r);
    }
    else {
      futex_wait(&t->q->qstats.total_enqueued, total_enqueued);
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
  futex_wake(&t->q->qstats.total_enqueued, count);
}

int tpool_get_current_active_threads(struct tpool *t)
{
  spinlock_lock(&t->lock);
  int active_threads = t->active_threads;
  spinlock_unlock(&t->lock);
  return active_threads;
}

double tpool_get_average_active_threads(struct tpool *t)
{
  spinlock_lock(&t->lock);
  double average = t->active_threads_samples ? 
                   t->active_threads_sum/t->active_threads_samples : 0;
  spinlock_unlock(&t->lock);
  return average;
}

void tpool_print_current_active_threads(struct tpool *t)
{
  int active_threads = tpool_get_current_active_threads(t);
  printf("Current active threads: %d\n", active_threads);
}

void tpool_print_average_active_threads(struct tpool *t)
{
  double average = tpool_get_average_active_threads(t);
  printf("Average active threads: %lf\n", average);
}

