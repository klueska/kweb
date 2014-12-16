#ifndef CPU_UTIL_H
#define CPU_UTIL_H

#include <unistd.h>
#include "os.h"

struct proc_util {
  double user;
  double sys;
};

struct cpu_util_stats {
  double cpu_time;
  double proc_user_time;
  double proc_sys_time;
};

struct cpu_util {
  int stat_fd;
  int proc_stat_fd;
  char buffer[1024];
  spin_pdr_lock_t lock;
  struct cpu_util_stats initial_stats;
};

void cpu_util_init(struct cpu_util *c);
void cpu_util_fini(struct cpu_util *c);

struct cpu_util_stats cpu_util_get_stats(struct cpu_util *c);
struct proc_util cpu_util_get_average(struct cpu_util_stats *prev,
                                      struct cpu_util_stats *curr);
void cpu_util_print_average(char *prefix,
                            struct cpu_util_stats *prev,
                            struct cpu_util_stats *curr);

#endif // CPU_UTIL_H

