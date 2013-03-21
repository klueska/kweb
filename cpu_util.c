#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include "cpu_util.h"

static int __set_cpu_time(struct cpu_util *c)
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
    c->stats.cpu_time = time;
  }
  return res;
}

static int __set_proc_time(struct cpu_util *c)
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
    c->stats.proc_user_time = atof(field);
    field = strtok_r(NULL, " ", &saveptr);
    c->stats.proc_sys_time = atof(field);
  }
  return res;
}

static void __cpu_util_update(struct cpu_util *c)
{
  __set_cpu_time(c);
  __set_proc_time(c);
}

void cpu_util_init(struct cpu_util *c)
{
  char proc_stat_file[20];
  c->stat_fd = open("/proc/stat", O_RDONLY);
  sprintf(proc_stat_file, "/proc/%d/stat", getpid());
  c->proc_stat_fd = open(proc_stat_file, O_RDONLY);
  memset(c->buffer, 0, sizeof(c->buffer));
  spinlock_init(&c->lock);

  c->stats.cpu_time = 0.0;
  c->stats.proc_user_time = 0.0;
  c->stats.proc_sys_time = 0.0;
}

void cpu_util_fini(struct cpu_util *c)
{
  close(c->stat_fd);
  close(c->proc_stat_fd);
}

struct cpu_util_stats cpu_util_get_stats(struct cpu_util *c)
{
  spinlock_lock(&c->lock);
  __cpu_util_update(c);
  struct cpu_util_stats s = c->stats;
  spinlock_unlock(&c->lock);
  return s;
}

struct proc_load cpu_util_get_average_load(struct cpu_util_stats *last,
                                           struct cpu_util_stats *current)
{
  struct proc_load proc_load;
  double cpu_time = current->cpu_time - last->cpu_time;
  double proc_user_time = current->proc_user_time - last->proc_user_time;
  double proc_sys_time = current->proc_sys_time - last->proc_sys_time;
  proc_load.user = 100*proc_user_time/cpu_time;
  proc_load.sys = 100*proc_sys_time/cpu_time;
  return proc_load;
}

void cpu_util_print_average_load(struct cpu_util_stats *last,
                                 struct cpu_util_stats *current)
{
  struct proc_load l = cpu_util_get_average_load(last, current);
  printf("Average user cpu load: %lf\n", l.user);
  printf("Average sys cpu load: %lf\n", l.sys);
  printf("Average total cpu load: %lf\n", l.user + l.sys);
}

