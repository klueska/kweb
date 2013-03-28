#define _GNU_SOURCE
#include <stdio.h>
#include <linux/futex.h>
#include <pthread.h>
#include <limits.h>
#include "futex.h"
#include "tpool.h"
#include "tsc.h"

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
    struct kitem *i = kqueue_dequeue_item(t->q);
    if(i == NULL) {
      t->stats.active_threads--;
      total_enqueued = t->q->qstats.total_enqueued;
    }
    spinlock_unlock(&t->lock);

    if(i) {
      uint64_t beg = read_tsc();
      t->func(t->q, i);
      uint64_t end = read_tsc();
      spinlock_lock(&t->lock);
      t->stats.processing_time_sum += (end - beg);
      t->stats.items_processed++;
      spinlock_unlock(&t->lock);
    }
    else {
      futex_wait(&t->q->qstats.total_enqueued, total_enqueued);
      spinlock_lock(&t->lock);
      t->stats.active_threads++;
      spinlock_unlock(&t->lock);
    }
    if(t->size > t->new_size) {
      __sync_fetch_and_add(&t->size, -1);
      spinlock_lock(&t->lock);
      t->stats.active_threads--;
      spinlock_unlock(&t->lock);
      return NULL;
    }
  }
}

static int create_threads(struct tpool *t, int num)
{
  int created = 0;
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN*4);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  for(int i=0; i<num; i++) {
    if(pthread_create(&thread, &attr, __thread_wrapper, t) == 0)
      created++;
    else
      break;
  }
  return created;
}

int tpool_init(struct tpool *t, int size, struct kqueue *q,
               void (*func)(struct kqueue *, struct kitem *))
{
  t->size = 0;
  t->new_size = 0;
  t->q = q;
  t->func = func;
  spinlock_init(&t->lock);
  t->stats.active_threads = 0;
  t->stats.active_threads_sum = 0;
  t->stats.active_threads_samples = 0;
  t->stats.items_processed = 0;
  t->stats.processing_time_sum = 0;
  t->size = create_threads(t, size);
  t->new_size = t->size;
  return t->size;
}

void tpool_resize(struct tpool *t, int size)
{
  int new_size = *((volatile int *)(&(t->new_size)));
  do {
    while(t->size != new_size) {
      pthread_yield();
      cpu_relax();
      new_size = *((volatile int *)(&(t->new_size)));
    }
  } while(!__sync_bool_compare_and_swap(&t->new_size, new_size, size));

  if(t->new_size < 1 || t->new_size == t->size)
    return;

  if(t->new_size > t->size) {
    t->size += create_threads(t, t->new_size - t->size);
  }
  else {
    futex_wake(&t->q->qstats.total_enqueued, INT_MAX);
    while(t->size != t->new_size) {
      pthread_yield();
      cpu_relax();
    }
  }
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

int tpool_get_items_processed(struct tpool_stats *prev,
                              struct tpool_stats *curr)
{
  return curr->items_processed - prev->items_processed;
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
  int items_processed = tpool_get_items_processed(prev, curr);
  double processing_time_sum = curr->processing_time_sum - prev->processing_time_sum;
  return items_processed ? processing_time_sum/items_processed : 0;
}

void tpool_print_items_processed(char *prefix,
                                 struct tpool_stats *prev,
                                 struct tpool_stats *curr)
{
  int items_processed = tpool_get_items_processed(prev, curr);
  printf("%sTotal items processed: %d\n", prefix, items_processed);
}

void tpool_print_average_active_threads(char *prefix,
                                        struct tpool_stats *prev,
                                        struct tpool_stats *curr)
{
  double average = tpool_get_average_active_threads(prev, curr);
  printf("%sAverage active threads: %lf\n", prefix, average);
}

void tpool_print_average_processing_time(char *prefix,
                                         struct tpool_stats *prev,
                                         struct tpool_stats *curr)
{
  double average = tpool_get_average_processing_time(prev, curr);
  printf("%sAverage item processing time: %lf\n", prefix, average);
}

