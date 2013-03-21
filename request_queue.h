#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include <sys/queue.h>
#include "spinlock.h"

struct request {
  SIMPLEQ_ENTRY(request) link;
  int id;
};

struct request_queue_stats {
  int size;
  double size_sum;
  int total_enqueued;
};

SIMPLEQ_HEAD(__request_queue, request);
struct request_queue {
  int request_size;

  spinlock_t lock;
  struct __request_queue queue;
  struct request_queue_stats qstats;

  spinlock_t zombie_lock;
  struct __request_queue zombie_queue;
  struct request_queue_stats zombie_qstats;
};

void request_queue_init(struct request_queue *q, int request_size);
void *request_queue_create_request(struct request_queue *q);
void request_queue_destroy_request(struct request_queue *q, struct request *r);
void request_queue_enqueue_request(struct request_queue *q, struct request *r);
struct request *request_queue_dequeue_request(struct request_queue *q);

struct request_queue_stats request_queue_get_stats(struct request_queue *q);
int request_queue_get_total_enqueued(struct request_queue_stats *last,
                                     struct request_queue_stats *current);
double request_queue_get_average_size(struct request_queue_stats *last,
                                      struct request_queue_stats *current);
void request_queue_print_total_enqueued(struct request_queue_stats *last,
                                        struct request_queue_stats *current);
void request_queue_print_average_size(struct request_queue_stats *last,
                                      struct request_queue_stats *current);

#endif // REQUEST_QUEUE_H
