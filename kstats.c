#include <stdio.h>
#include <string.h>
#include "kstats.h"
#include "tsc.h"

static void ktimer_callback(void *arg);
static void print_interval_statistics(struct kstats *kstats);

void kstats_init(struct kstats *kstats, struct kqueue *kqueue,
                 struct tpool *tpool, struct cpu_util *cpu_util)
{
  kstats->kqueue = kqueue;
  kstats->tpool = tpool;
  kstats->cpu_util = cpu_util;

  ktimer_init(&kstats->ktimer, ktimer_callback, kstats);
  memset(&kstats->rqstats_prev, 0, sizeof(struct kqueue_stats)); 
  memset(&kstats->rqstats_curr, 0, sizeof(struct kqueue_stats));
  memset(&kstats->tpstats_prev, 0, sizeof(struct kqueue_stats));
  memset(&kstats->tpstats_curr, 0, sizeof(struct tpool_stats));
  memset(&kstats->custats_prev, 0, sizeof(struct tpool_stats));
  memset(&kstats->custats_curr, 0, sizeof(struct cpu_util_stats));
}

int kstats_start(struct kstats *k, unsigned int period_ms)
{
  return ktimer_start(&k->ktimer, period_ms);
}

int kstats_stop(struct kstats *k)
{
  return ktimer_stop(&k->ktimer);
}

static void ktimer_callback(void *arg)
{
  struct kstats *k = (struct kstats*)arg;
  k->rqstats_prev = k->rqstats_curr;
  k->tpstats_prev = k->tpstats_curr;
  k->custats_prev = k->custats_curr;

  k->rqstats_curr = kqueue_get_stats(k->kqueue);
  k->tpstats_curr = tpool_get_stats(k->tpool);
  k->custats_curr = cpu_util_get_stats(k->cpu_util);

  print_interval_statistics(k);
}

static void print_statistics(char *prefix,
                             struct kqueue_stats *rqprev,
                             struct kqueue_stats *rqcurr,
                             struct tpool_stats *tpprev,
                             struct tpool_stats *tpcurr,
                             struct cpu_util_stats *cuprev,
                             struct cpu_util_stats *cucurr)
{
  printf("%sTimestamp: %lu\n", prefix, read_tsc());
  kqueue_print_total_enqueued(prefix, rqprev, rqcurr);
  tpool_print_items_processed(prefix, tpprev, tpcurr);
  tpool_print_average_active_threads(prefix, tpprev, tpcurr);
  tpool_print_average_blocked_threads(prefix, tpprev, tpcurr);
  kqueue_print_average_size(prefix, rqprev, rqcurr);
  kqueue_print_average_wait_time(prefix, rqprev, rqcurr);
  tpool_print_average_processing_time(prefix, tpprev, tpcurr);
  cpu_util_print_average(prefix, cuprev, cucurr);
}

static void print_interval_statistics(struct kstats *k)
{
  printf("\n");
  printf("Interval Statistics:\n");
  print_statistics("  ",
                   &k->rqstats_prev, &k->rqstats_curr,
                   &k->tpstats_prev, &k->tpstats_curr,
                   &k->custats_prev, &k->custats_curr);
}

void kstats_print_lifetime_statistics(struct kstats *k)
{
  printf("\n");
  printf("Lifetime Statistics:\n");
  print_statistics("  ",
                   &((struct kqueue_stats){0}), &k->rqstats_curr,
                   &((struct tpool_stats){0}), &k->tpstats_curr,
                   &((struct cpu_util_stats){0}), &k->custats_curr);
}

