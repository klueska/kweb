#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include "cpu_util.h"

enum {
  CPU_UTIL_STARTING,
  CPU_UTIL_STARTED,
  CPU_UTIL_STOPPING,
  CPU_UTIL_STOPPED,
};

static int __set_cpu_time(struct cpu_util *c, double *cpu_time)
{
  int res = 0;
  double time = 0;

  lseek(c->stat_fd, 0, SEEK_SET);
  res = read(c->stat_fd, c->buffer, sizeof(c->buffer));

  if(res > 0) {
    char *line = strtok(c->buffer, "\n");
    if(line == NULL)
      return -1;
    char *field = strtok(line, " ");
    field = strtok(NULL, " ");
    while(field != NULL) {
      time += atof(field);
      field = strtok(NULL, " ");
    }
    *cpu_time = time;
  }
  return res;
}

static int __set_proc_time(struct cpu_util *c, struct proc_stats *proc_time)
{
  int res = 0;
  struct proc_stats time = {0, 0};

  lseek(c->proc_stat_fd, 0, SEEK_SET);
  res = read(c->proc_stat_fd, c->buffer, sizeof(c->buffer));

  if(res > 0) {
    char *field = strtok(c->buffer, " ");
    for(int i=1; i<14; i++)
      field = strtok(NULL, " ");
    if(field == NULL)
      return -1;
    time.user = atof(field);
    field = strtok(NULL, " ");
    if(field == NULL)
      return -1;
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

static void *__periodic_update(void *arg)
{
  struct cpu_util *c = (struct cpu_util*)arg;

  c->state = CPU_UTIL_STARTED;
  __set_cpu_time(c, &c->cpu_time_after);
  __set_proc_time(c, &c->proc_time_after);

  for(;;) {
    usleep(1000*c->period_ms);

    spinlock_lock(&c->lock);
    if(c->state == CPU_UTIL_STOPPING)
      break;
    __cpu_util_update(c);
    spinlock_unlock(&c->lock);

    if(c->callback)
      c->callback(c, c->callback_arg);
  }
  c->state = CPU_UTIL_STOPPED;
  spinlock_unlock(&c->lock);
}

void cpu_util_init(struct cpu_util *c, useconds_t period_ms,
                   void (*callback)(struct cpu_util*, void*), void* arg)
{
  c->state = CPU_UTIL_STOPPED;
  c->period_ms = period_ms;
  c->callback = callback;
  c->callback_arg = arg;

  spinlock_init(&c->lock);

  c->stat_fd = -1;
  c->proc_stat_fd = -1;
  memset(c->buffer, 0, sizeof(c->buffer));

  c->proc_util_samples = 0;
  c->proc_util_current.user = 0.0;
  c->proc_util_current.sys = 0.0;
  c->proc_util_sum.user = 0.0;
  c->proc_util_sum.sys = 0.0;

  c->cpu_time_before = 0.0;
  c->proc_time_before.user = 0.0;
  c->proc_time_before.sys = 0.0;

  c->cpu_time_after = 0.0;
  c->proc_time_after.user = 0.0;
  c->proc_time_after.sys = 0.0;
}

int cpu_util_start(struct cpu_util *c)
{
  if(c->state != CPU_UTIL_STOPPED)
    return -1;
  c->state = CPU_UTIL_STARTING;

  char proc_stat_file[20];
  c->stat_fd = open("/proc/stat", O_RDONLY);
  sprintf(proc_stat_file, "/proc/%d/stat", getpid());
  c->proc_stat_fd = open(proc_stat_file, O_RDONLY);

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&thread, &attr, __periodic_update, c);
}

int cpu_util_stop(struct cpu_util *c)
{
  if(c->state != CPU_UTIL_STARTED)
    return -1;

  spinlock_lock(&c->lock);
  c->state = CPU_UTIL_STOPPING;
  spinlock_unlock(&c->lock);

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

