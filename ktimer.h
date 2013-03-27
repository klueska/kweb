#ifndef KTIMER_H
#define KTIMER_H

#include "spinlock.h"

struct ktimer {
  int state;
  long period_ms;
  void (*callback)(void*);
  void *callback_arg;
  spinlock_t lock;
};

void ktimer_init(struct ktimer *t, void (*callback)(void*), void* arg);
int ktimer_start(struct ktimer *t, int period_ms);
int ktimer_stop(struct ktimer *t);

#endif // KTIMER_H

