#include <malloc.h>
#include "request_queue.h"

void request_queue_init(struct request_queue *q, int request_size)
{
  q->request_size = request_size;

  SIMPLEQ_INIT(&q->queue);
  spinlock_init(&q->lock);
  q->qstats.size = 0;
  q->qstats.size_sum = 0;
  q->qstats.total_enqueued = 0;
  q->qstats.total_dequeued = 0;
  q->qstats.wait_time_sum = 0;

  SIMPLEQ_INIT(&q->zombie_queue);
  spinlock_init(&q->zombie_lock);
  q->zombie_qstats.size = 0;
  q->zombie_qstats.size_sum = 0;
  q->zombie_qstats.total_enqueued = 0;
  q->zombie_qstats.total_dequeued = 0;
  q->zombie_qstats.wait_time_sum = 0;
}

void *request_queue_create_request(struct request_queue *q)
{
  spinlock_lock(&q->zombie_lock);
  struct request *r = SIMPLEQ_FIRST(&q->zombie_queue);
  if(r) {
    SIMPLEQ_REMOVE_HEAD(&q->zombie_queue, link);
    r->dequeue_time = read_tsc();
    q->zombie_qstats.total_dequeued++;
    q->zombie_qstats.wait_time_sum += (r->dequeue_time - r->enqueue_time);
    q->zombie_qstats.size--;
  }
  spinlock_unlock(&q->zombie_lock);

  if(r == NULL) {
    r = malloc(q->request_size);
  }
  r->id = 0;
  r->enqueue_time = 0;
  r->dequeue_time = 0;
  return r;
}

void request_queue_destroy_request(struct request_queue *q, struct request *r)
{
  spinlock_lock(&q->zombie_lock);
  q->zombie_qstats.size_sum += q->zombie_qstats.size++;
  r->id = q->zombie_qstats.total_enqueued++;
  r->enqueue_time = read_tsc();
  SIMPLEQ_INSERT_HEAD(&q->zombie_queue, r, link);
  spinlock_unlock(&q->zombie_lock);
}

void request_queue_enqueue_request(struct request_queue *q, struct request *r)
{
  spinlock_lock(&q->lock);
  q->qstats.size_sum += q->qstats.size++;
  r->id = q->qstats.total_enqueued++;
  r->enqueue_time = read_tsc();
  SIMPLEQ_INSERT_TAIL(&q->queue, r, link);
  spinlock_unlock(&q->lock);
}

struct request *request_queue_dequeue_request(struct request_queue *q)
{
  spinlock_lock(&q->lock);
  struct request *r = SIMPLEQ_FIRST(&q->queue);
  if(r) {
    SIMPLEQ_REMOVE_HEAD(&q->queue, link);
    r->dequeue_time = read_tsc();
    q->qstats.total_dequeued++;
    q->qstats.wait_time_sum += (r->dequeue_time - r->enqueue_time);
    q->qstats.size--;
  }
  spinlock_unlock(&q->lock);
  return r;
}

struct request_queue_stats request_queue_get_stats(struct request_queue *q)
{
  spinlock_lock(&q->lock);
  struct request_queue_stats s = q->qstats;
  spinlock_unlock(&q->lock);
  return s;
}

double request_queue_get_average_size(struct request_queue_stats *prev,
                                      struct request_queue_stats *curr)
{
  int total_enqueued = curr->total_enqueued - prev->total_enqueued; 
  double size_sum = curr->size_sum - prev->size_sum; 
  return total_enqueued ? size_sum/total_enqueued : 0;
}

int request_queue_get_total_enqueued(struct request_queue_stats *prev,
                                     struct request_queue_stats *curr)
{
  return curr->total_enqueued - prev->total_enqueued; 
}

double request_queue_get_average_wait_time(struct request_queue_stats *prev,
                                           struct request_queue_stats *curr)
{
  double wait_time_sum = curr->wait_time_sum - prev->wait_time_sum;
  uint64_t total_dequeued = curr->total_dequeued - prev->total_dequeued;
  return total_dequeued ? wait_time_sum/total_dequeued : 0;
}

void request_queue_print_total_enqueued(struct request_queue_stats *prev,
                                        struct request_queue_stats *curr)
{
  int total_enqueued = request_queue_get_total_enqueued(prev, curr);
  printf("Total requests enqueued: %d\n", total_enqueued);
}

void request_queue_print_average_size(struct request_queue_stats *prev,
                                      struct request_queue_stats *curr)
{
  double average = request_queue_get_average_size(prev, curr);
  printf("Average request queue length: %lf\n", average);
}

void request_queue_print_average_wait_time(struct request_queue_stats *prev,
                                           struct request_queue_stats *curr)
{
  double average = request_queue_get_average_wait_time(prev, curr);
  printf("Average wait time in request queue: %lf\n", average);
}

