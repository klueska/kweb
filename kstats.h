#ifndef KSTATS_H
#define KSTATS_H

#include "kqueue.h"
#include "tpool.h"
#include "cpu_util.h"
#include "ktimer.h"

struct kstats {
  struct kqueue *kqueue;
  struct tpool *tpool;
  struct cpu_util * cpu_util;

  struct ktimer ktimer;
  struct kqueue_stats rqstats_prev;
  struct kqueue_stats rqstats_curr;
  struct tpool_stats tpstats_prev;
  struct tpool_stats tpstats_curr;
  struct cpu_util_stats custats_prev;
  struct cpu_util_stats custats_curr;
};

void kstats_init(struct kstats *kstats, struct kqueue *kqueue,
                 struct tpool *tpool, struct cpu_util *cpu_util);
int kstats_start(struct kstats *kstats, unsigned int period_ms);
int kstats_stop(struct kstats *kstats);
void kstats_print_lifetime_statistics(struct kstats *kstats);

#endif // KSTATS_H
