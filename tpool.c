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
  t->stats.active_threads++;
  spinlock_unlock(&t->lock);

  while(1) {
    spinlock_lock(&t->lock);
    t->stats.active_threads_sum += t->stats.active_threads;
    t->stats.active_threads_samples++;
    struct request *r = request_queue_dequeue_request(t->q);
    if(r == NULL) {
      t->stats.active_threads--;
      total_enqueued = t->q->qstats.total_enqueued;
    }
    spinlock_unlock(&t->lock);

    if(r) {
      uint64_t beg = read_tsc();
      t->func(t->q, r);
      uint64_t end = read_tsc();
      spinlock_lock(&t->lock);
      t->stats.processing_time_sum += (end - beg);
      t->stats.requests_processed++;
      spinlock_unlock(&t->lock);
    }
    else {
      futex_wait(&t->q->qstats.total_enqueued, total_enqueued);
      spinlock_lock(&t->lock);
      t->stats.active_threads++;
      spinlock_unlock(&t->lock);
    }
  }
}

int tpool_init(struct tpool *t, int size, struct request_queue *q,
               void (*func)(struct request_queue *, struct request *))
{
  t->size = 0;
  t->q = q;
  t->func = func;
  spinlock_init(&t->lock);
  t->stats.active_threads = 0;
  t->stats.active_threads_sum = 0;
  t->stats.active_threads_samples = 0;
  t->stats.requests_processed = 0;
  t->stats.processing_time_sum = 0;

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

struct tpool_stats tpool_get_stats(struct tpool *t)
{
  spinlock_lock(&t->lock);
  struct tpool_stats s = t->stats;
  spinlock_unlock(&t->lock);
  return s;
}

int tpool_get_requests_processed(struct tpool_stats *prev,
                                 struct tpool_stats *curr)
{
  return curr->requests_processed - prev->requests_processed;
}

double tpool_get_average_active_threads(struct tpool_stats *prev,
                                        struct tpool_stats *curr)
{
  int active_threads_samples = curr->active_threads_samples - prev->active_threads_samples;
  double active_threads_sum = curr->active_threads_sum - prev->active_threads_sum;
  return active_threads_samples ? active_threads_sum/active_threads_samples : 0;
}

double tpool_get_average_processing_time(struct tpool_stats *prev,
                                         struct tpool_stats *curr)
{
  int requests_processed = tpool_get_requests_processed(prev, curr);
  double processing_time_sum = curr->processing_time_sum - prev->processing_time_sum;
  return requests_processed ? processing_time_sum/requests_processed : 0;
}

void tpool_print_requests_processed(struct tpool_stats *prev,
                                    struct tpool_stats *curr)
{
  int requests_processed = tpool_get_requests_processed(prev, curr);
  printf("Total requests processed: %d\n", requests_processed);
}

void tpool_print_average_active_threads(struct tpool_stats *prev,
                                        struct tpool_stats *curr)
{
  double average = tpool_get_average_active_threads(prev, curr);
  printf("Average active threads: %lf\n", average);
}

void tpool_print_average_processing_time(struct tpool_stats *prev,
                                         struct tpool_stats *curr)
{
  double average = tpool_get_average_processing_time(prev, curr);
  printf("Average request processing time: %lf\n", average);
}

