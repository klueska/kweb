#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include <sys/queue.h>
#include "spinlock.h"

struct request {
  SIMPLEQ_ENTRY(request) link;
  int id;
};
SIMPLEQ_HEAD(__request_queue, request);
struct request_queue {
  int request_size;

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

void request_queue_init(struct request_queue *q, int request_size);
void *request_queue_create_request(struct request_queue *q);
void request_queue_destroy_request(struct request_queue *q, struct request *r);
void request_queue_enqueue_request(struct request_queue *q, struct request *r);
struct request *request_queue_dequeue_request(struct request_queue *q);

#endif // REQUEST_QUEUE_H
