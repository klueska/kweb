#define _GNU_SOURCE
#include <unistd.h>
#include <pthread.h>
#include "ktimer.h"

enum {
  S_TIMER_STARTING,
  S_TIMER_STARTED,
  S_TIMER_STOPPING,
  S_TIMER_STOPPED,
};

static void *__ktimer(void *arg)
{
  struct ktimer *t = (struct ktimer*)arg;

  t->state = S_TIMER_STARTED;
  for(;;) {
    usleep(1000*t->period_ms);

    spin_pdr_lock(&t->lock);
    if(t->state == S_TIMER_STOPPING)
      break;
    spin_pdr_unlock(&t->lock);
    t->callback(t->callback_arg);
  }
  t->state = S_TIMER_STOPPED;
  spin_pdr_unlock(&t->lock);
}

void ktimer_init(struct ktimer *t, void (*callback)(void*), void* arg)
{
  t->state = S_TIMER_STOPPED;
  t->callback = callback;
  t->callback_arg = arg;
  spin_pdr_init(&t->lock);
}

int ktimer_start(struct ktimer *t, unsigned int period_ms)
{
  if(t->state != S_TIMER_STOPPED)
    return -1;
  t->state = S_TIMER_STARTING;
  t->period_ms = period_ms;

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&thread, &attr, __ktimer, t);
}

int ktimer_stop(struct ktimer *t)
{
  if(t->state != S_TIMER_STARTED)
    return -1;

  spin_pdr_lock(&t->lock);
  t->state = S_TIMER_STOPPING;
  spin_pdr_unlock(&t->lock);
  while(t->state != S_TIMER_STOPPED) {
    pthread_yield();
    cpu_relax();
  }
}

