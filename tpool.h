#ifndef TPOOL_H
#define TPOOL_H

#include "spinlock.h"
#include "request_queue.h"

struct tpool {
  int size;
  struct request_queue *q;
  void (*func)(struct request_queue *, struct request *);

  spinlock_t lock;

  int active_threads;
  double active_threads_sum;
  int active_threads_samples;
};

int tpool_init(struct tpool *t, int size, struct request_queue *q,
               void (*func)(struct request_queue *, struct request *));
void tpool_wake(struct tpool *t, int count);
int tpool_get_current_active_threads(struct tpool *t);
double tpool_get_average_active_threads(struct tpool *t);
void tpool_print_current_active_threads(struct tpool *t);
void tpool_print_average_active_threads(struct tpool *t);

#endif // TPOOL_H
