#ifndef TPOOL_H
#define TPOOL_H

#include <sys/queue.h>
#include <linux/futex.h>
#include <pthread.h>
#include "spinlock.h"

struct request {
  SIMPLEQ_ENTRY(request) link;
  int id;
};
SIMPLEQ_HEAD(__request_queue, request);
struct request_queue {
  int request_size;
  void (*func)(struct request_queue *, struct request *);

  int total_enqueued;
  int size;
  double size_sum;
  spinlock_t lock;
  struct __request_queue queue;

  int zombie_total_enqueued;
  int zombie_size;
  double zombie_size_sum;
  spinlock_t zombie_lock;
  struct __request_queue zombie_queue;
};

void request_queue_init(struct request_queue *q, 
                        void (*func)(struct request_queue *, struct request *),
                        int request_size);
int tpool_init(struct request_queue *q, int num);
void tpool_wake(struct request_queue *q, int count);
void *create_request(struct request_queue *q);
void destroy_request(struct request_queue *q, struct request *r);
void enqueue_request(struct request_queue *q, struct request *r);

#endif // TSTACK_H
