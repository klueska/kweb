#ifndef TPOOL_H
#define TPOOL_H

#include "spinlock.h"
#include "kqueue.h"

struct tpool_stats {
  int active_threads;
  double active_threads_sum;
  int active_threads_samples;
  int items_processed;
  double processing_time_sum;
};

struct tpool {
  int size;
  int new_size;
  struct kqueue *q;
  void (*func)(struct kqueue *, struct kitem *);
  spinlock_t lock;
  struct tpool_stats stats;
};

int tpool_init(struct tpool *t, int size, struct kqueue *q,
               void (*func)(struct kqueue *, struct kitem *));
void tpool_resize(struct tpool *t, int size);
void tpool_wake(struct tpool *t, int count);

struct tpool_stats tpool_get_stats(struct tpool *t);
int tpool_get_items_processed(struct tpool_stats *prev,
                              struct tpool_stats *curr);
double tpool_get_average_active_threads(struct tpool_stats *prev,
                                        struct tpool_stats *curr);
double tpool_get_average_processing_time(struct tpool_stats *prev,
                                         struct tpool_stats *curr);
void tpool_print_items_processed(char *prefix,
                                 struct tpool_stats *prev,
                                 struct tpool_stats *curr);
void tpool_print_average_active_threads(char *prefix,
                                        struct tpool_stats *prev,
                                        struct tpool_stats *curr);
void tpool_print_average_processing_time(char *prefix,
                                         struct tpool_stats *prev,
                                         struct tpool_stats *curr);

#endif // TPOOL_H
