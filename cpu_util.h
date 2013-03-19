#ifndef CPU_UTIL_H
#define CPU_UTIL_H

#include <unistd.h>
#include "spinlock.h"

struct proc_stats {
  double user;
  double sys;
};

struct cpu_util {
  int state;
  long period_ms;
  void (*callback)(struct cpu_util*, void*);
  void *callback_arg;

  spinlock_t lock;

  int stat_fd;
  int proc_stat_fd;
  char buffer[1024];

  long proc_util_samples;
  struct proc_stats proc_util_current;
  struct proc_stats proc_util_sum;

  double cpu_time_before;
  struct proc_stats proc_time_before;

  double cpu_time_after;
  struct proc_stats proc_time_after;
};

void cpu_util_init(struct cpu_util *c, useconds_t period_ms,
                   void (*callback)(struct cpu_util*, void*), void* arg);
int cpu_util_start(struct cpu_util *c);
int cpu_util_stop(struct cpu_util *c);
void cpu_util_update(struct cpu_util *c);
struct proc_stats cpu_util_get_current(struct cpu_util *c);
struct proc_stats cpu_util_get_average(struct cpu_util *c);
void cpu_util_print_current(struct cpu_util *c);
void cpu_util_print_average(struct cpu_util *c);

#endif // CPU_UTIL_H

