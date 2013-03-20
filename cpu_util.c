#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include "cpu_util.h"

static int __set_cpu_time(struct cpu_util *c, double *cpu_time)
{
  int res = 0;
  double time = 0;
  char *saveptr;

  lseek(c->stat_fd, 0, SEEK_SET);
  res = read(c->stat_fd, c->buffer, sizeof(c->buffer));

  if(res > 0) {
    char *line = strtok_r(c->buffer, "\n", &saveptr);
    if(line == NULL)
      return -1;
    char *field = strtok_r(line, " ", &saveptr);
    field = strtok_r(NULL, " ", &saveptr);
    while(field != NULL) {
      time += atof(field);
      field = strtok_r(NULL, " ", &saveptr);
    }
    *cpu_time = time;
  }
  return res;
}

static int __set_proc_time(struct cpu_util *c, struct proc_stats *proc_time)
{
  int res = 0;
  struct proc_stats time = {0, 0};
  char *saveptr;

  lseek(c->proc_stat_fd, 0, SEEK_SET);
  res = read(c->proc_stat_fd, c->buffer, sizeof(c->buffer));

  if(res > 0) {
    char *line = strtok_r(c->buffer, "\n", &saveptr);
    if(line == NULL)
      return -1;
    char *field = strtok_r(c->buffer, " ", &saveptr);
    for(int i=1; i<14; i++)
      field = strtok_r(NULL, " ", &saveptr);
    time.user = atof(field);
    field = strtok_r(NULL, " ", &saveptr);
    time.sys = atof(field);
    *proc_time = time;
  }
  return res;
}

static void __cpu_util_update(struct cpu_util *c)
{
  c->cpu_time_before = c->cpu_time_after;
  c->proc_time_before = c->proc_time_after;

  int res1 = __set_cpu_time(c, &c->cpu_time_after);
  int res2 = __set_proc_time(c, &c->proc_time_after);
  
  if((res1 > 0) && (res2 > 0)) {
    double cpu_time_diff = c->cpu_time_after - c->cpu_time_before;
    double user_time_diff = c->proc_time_after.user - c->proc_time_before.user;
    double sys_time_diff = c->proc_time_after.sys - c->proc_time_before.sys;
    if((cpu_time_diff > 0) && (user_time_diff > 0) && (sys_time_diff > 0)) {
      double user_util = 100*user_time_diff/cpu_time_diff;
      double sys_util = 100*sys_time_diff/cpu_time_diff;
      if((user_util+sys_util) <= 100) {
        c->proc_util_samples++;
        c->proc_util_current.user = user_util;
        c->proc_util_current.sys = sys_util;
        c->proc_util_sum.user += user_util;
        c->proc_util_sum.sys += sys_util;
      }
    }
  }
}

static void __cpu_util_print_current(struct cpu_util *c)
{
  double total = c->proc_util_current.user + c->proc_util_current.sys;
  printf("Current user cpu utilization: %lf%%\n", c->proc_util_current.user);
  printf("Current system cpu utilization: %lf%%\n", c->proc_util_current.sys);
  printf("Current total cpu utilization: %lf%%\n", total);
}

static void __cpu_util_print_average(struct cpu_util *c)
{
  double user_average = c->proc_util_samples ? 
           c->proc_util_sum.user/c->proc_util_samples : 0;
  printf("Average user cpu utilization: %lf%%\n", user_average);

  double sys_average = c->proc_util_samples ? 
              c->proc_util_sum.sys/c->proc_util_samples : 0;
  printf("Average system cpu utilization: %lf%%\n", sys_average);

  double total_average = user_average + sys_average;
  printf("Average total cpu utilization: %lf%%\n", total_average);
}

void cpu_util_init(struct cpu_util *c)
{
  char proc_stat_file[20];
  c->stat_fd = open("/proc/stat", O_RDONLY);
  sprintf(proc_stat_file, "/proc/%d/stat", getpid());
  c->proc_stat_fd = open(proc_stat_file, O_RDONLY);
  memset(c->buffer, 0, sizeof(c->buffer));

  spinlock_init(&c->lock);

  c->proc_util_samples = 0;
  c->proc_util_current.user = 0.0;
  c->proc_util_current.sys = 0.0;
  c->proc_util_sum.user = 0.0;
  c->proc_util_sum.sys = 0.0;

  c->cpu_time_before = 0.0;
  c->proc_time_before.user = 0.0;
  c->proc_time_before.sys = 0.0;

  __set_cpu_time(c, &c->cpu_time_after);
  __set_proc_time(c, &c->proc_time_after);
}

void cpu_util_fini(struct cpu_util *c)
{
  close(c->stat_fd);
  close(c->proc_stat_fd);
}

void cpu_util_update(struct cpu_util *c)
{
  spinlock_lock(&c->lock);
  __cpu_util_update(c);
  spinlock_unlock(&c->lock);
}

struct proc_stats cpu_util_get_current(struct cpu_util *c)
{
  struct proc_stats p;
  spinlock_lock(&c->lock);
  p = c->proc_util_current;
  spinlock_unlock(&c->lock);
  return p;
}

struct proc_stats cpu_util_get_average(struct cpu_util *c)
{
  struct proc_stats p;
  spinlock_lock(&c->lock);
  p.user = c->proc_util_samples ? 
           c->proc_util_sum.user/c->proc_util_samples : 0;
  p.sys = c->proc_util_samples ? 
          c->proc_util_sum.sys/c->proc_util_samples : 0;
  spinlock_unlock(&c->lock);
  return p;
}

void cpu_util_print_current(struct cpu_util *c)
{
  spinlock_lock(&c->lock);
  __cpu_util_print_current(c);
  spinlock_unlock(&c->lock);
}

void cpu_util_print_average(struct cpu_util *c)
{
  spinlock_lock(&c->lock);
  __cpu_util_print_average(c);
  spinlock_unlock(&c->lock);
}
