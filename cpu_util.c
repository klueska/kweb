#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include "cpu_util.h"

static int __set_cpu_time(struct cpu_util *c, struct cpu_util_stats *s)
{
  int res = 0;
  char *saveptr;
  double time;

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
    s->cpu_time = time;
  }
  return res;
}

static int __set_proc_time(struct cpu_util *c, struct cpu_util_stats *s)
{
  int res = 0;
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
    s->proc_user_time = atof(field);
    field = strtok_r(NULL, " ", &saveptr);
    s->proc_sys_time = atof(field);
  }
  return res;
}

void cpu_util_init(struct cpu_util *c)
{
  char proc_stat_file[20];
  c->stat_fd = open("/proc/stat", O_RDONLY);
  sprintf(proc_stat_file, "/proc/%d/stat", getpid());
  c->proc_stat_fd = open(proc_stat_file, O_RDONLY);
  memset(c->buffer, 0, sizeof(c->buffer));
  spin_pdr_init(&c->lock);
  __set_cpu_time(c, &c->initial_stats);
  __set_proc_time(c, &c->initial_stats);
}

void cpu_util_fini(struct cpu_util *c)
{
  close(c->stat_fd);
  close(c->proc_stat_fd);
}

struct cpu_util_stats cpu_util_get_stats(struct cpu_util *c)
{
  struct cpu_util_stats s;
  spin_pdr_lock(&c->lock);
  __set_cpu_time(c, &s);
  __set_proc_time(c, &s);
  s.cpu_time -= c->initial_stats.cpu_time;
  s.proc_user_time -= c->initial_stats.proc_user_time;
  s.proc_sys_time -= c->initial_stats.proc_sys_time;
  spin_pdr_unlock(&c->lock);
  return s;
}

struct proc_util cpu_util_get_average(struct cpu_util_stats *prev,
                                      struct cpu_util_stats *curr)
{
  struct proc_util proc_util;
  double cpu_time = curr->cpu_time - prev->cpu_time;
  double proc_user_time = curr->proc_user_time - prev->proc_user_time;
  double proc_sys_time = curr->proc_sys_time - prev->proc_sys_time;
  proc_util.user = cpu_time ? 100*proc_user_time/cpu_time : 0;
  proc_util.sys = cpu_time ? 100*proc_sys_time/cpu_time : 0;
  return proc_util;
}

void cpu_util_print_average(char *prefix,
                            struct cpu_util_stats *prev,
                            struct cpu_util_stats *curr)
{
  struct proc_util l = cpu_util_get_average(prev, curr);
  printf("%sAverage user cpu utilization: %lf\n", prefix, l.user);
  printf("%sAverage sys cpu utilization: %lf\n", prefix, l.sys);
  printf("%sAverage total cpu utilization: %lf\n", prefix, l.user + l.sys);
}

