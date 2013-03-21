#ifndef CPU_UTIL_H
#define CPU_UTIL_H

#include <unistd.h>
#include "spinlock.h"

struct proc_load {
  double user;
  double sys;
};

struct cpu_util_stats {
  double initial_cpu_time;
  double initial_proc_user_time;
  double initial_proc_sys_time;

  double cpu_time;
  double proc_user_time;
  double proc_sys_time;
};

struct cpu_util {
  int stat_fd;
  int proc_stat_fd;
  char buffer[1024];
  spinlock_t lock;
  struct cpu_util_stats stats;
};

void cpu_util_init(struct cpu_util *c);
void cpu_util_fini(struct cpu_util *c);

struct cpu_util_stats cpu_util_get_stats(struct cpu_util *c);
struct proc_load cpu_util_get_average_load(struct cpu_util_stats *last,
                                           struct cpu_util_stats *current);
void cpu_util_print_average_load(struct cpu_util_stats *last,
                                 struct cpu_util_stats *current);

#endif // CPU_UTIL_H

