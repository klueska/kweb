#ifndef TPOOL_H
#define TPOOL_H

#include "spinlock.h"
#include "request_queue.h"

struct tpool_stats {
  int active_threads;
  double active_threads_sum;
  int active_threads_samples;
  int requests_processed;
};

struct tpool {
  int size;
  struct request_queue *q;
  void (*func)(struct request_queue *, struct request *);
  spinlock_t lock;
  struct tpool_stats stats;
};

int tpool_init(struct tpool *t, int size, struct request_queue *q,
               void (*func)(struct request_queue *, struct request *));
void tpool_wake(struct tpool *t, int count);

struct tpool_stats tpool_get_stats(struct tpool *t);
int tpool_get_requests_processed(struct tpool_stats *prev,
                                 struct tpool_stats *curr);
double tpool_get_average_active_threads(struct tpool_stats *prev,
                                        struct tpool_stats *curr);
void tpool_print_requests_processed(struct tpool_stats *prev,
                                    struct tpool_stats *curr);
void tpool_print_average_active_threads(struct tpool_stats *prev,
                                        struct tpool_stats *curr);

#endif // TPOOL_H
