#ifndef KQUEUE_H
#define KQUEUE_H

#include <stdint.h>
#include <sys/queue.h>
#include "spinlock.h"

struct kitem {
  SIMPLEQ_ENTRY(kitem) link;
  long id;
  uint64_t enqueue_time;
  uint64_t dequeue_time;
};

struct kqueue_stats {
  int size;
  double size_sum;
  int total_enqueued;
  int total_dequeued;
  double wait_time_sum;
};

SIMPLEQ_HEAD(__kqueue, kitem);
struct kqueue {
  int item_size;
  long ids;

  spinlock_t lock;
  struct __kqueue queue;
  struct kqueue_stats qstats;

  spinlock_t zombie_lock;
  struct __kqueue zombie_queue;
  struct kqueue_stats zombie_qstats;
};

void kqueue_init(struct kqueue *q, int item_size);
void *kqueue_create_item(struct kqueue *q);
void kqueue_destroy_item(struct kqueue *q, struct kitem *r);
void kqueue_enqueue_item(struct kqueue *q, struct kitem *r);
struct kitem *kqueue_dequeue_item(struct kqueue *q);

struct kqueue_stats kqueue_get_stats(struct kqueue *q);
int kqueue_get_total_enqueued(struct kqueue_stats *prev,
                              struct kqueue_stats *curr);
double kqueue_get_average_size(struct kqueue_stats *prev,
                               struct kqueue_stats *curr);
double kqueue_get_average_wait_time(struct kqueue_stats *prev,
                                    struct kqueue_stats *curr);
void kqueue_print_total_enqueued(char* prefix,
                                 struct kqueue_stats *prev,
                                 struct kqueue_stats *curr);
void kqueue_print_average_size(char* prefix,
                               struct kqueue_stats *prev,
                               struct kqueue_stats *curr);
void kqueue_print_average_wait_time(char* prefix,
                                    struct kqueue_stats *prev,
                                    struct kqueue_stats *curr);

#endif // KQUEUE_H
