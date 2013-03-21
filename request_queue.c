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

  SIMPLEQ_INIT(&q->zombie_queue);
  spinlock_init(&q->zombie_lock);
  q->zombie_qstats.size = 0;
  q->zombie_qstats.size_sum = 0;
  q->zombie_qstats.total_enqueued = 0;
}

void *request_queue_create_request(struct request_queue *q)
{
  spinlock_lock(&q->zombie_lock);
  struct request *r = SIMPLEQ_FIRST(&q->zombie_queue);
  if(r) {
    q->zombie_qstats.size--;
    SIMPLEQ_REMOVE_HEAD(&q->zombie_queue, link);
  }
  spinlock_unlock(&q->zombie_lock);

  if(r == NULL) {
    r = malloc(q->request_size);
  }
  r->id = 0;
  return r;
}

void request_queue_destroy_request(struct request_queue *q, struct request *r)
{
  spinlock_lock(&q->zombie_lock);
  q->zombie_qstats.size_sum += q->zombie_qstats.size++;
  q->zombie_qstats.total_enqueued++;
  SIMPLEQ_INSERT_HEAD(&q->zombie_queue, r, link);
  spinlock_unlock(&q->zombie_lock);
}

void request_queue_enqueue_request(struct request_queue *q, struct request *r)
{
  spinlock_lock(&q->lock);
  q->qstats.size_sum += q->qstats.size++;
  r->id = q->qstats.total_enqueued++;
  SIMPLEQ_INSERT_TAIL(&q->queue, r, link);
  spinlock_unlock(&q->lock);
}

struct request *request_queue_dequeue_request(struct request_queue *q)
{
  spinlock_lock(&q->lock);
  struct request *r = SIMPLEQ_FIRST(&q->queue);
  if(r) {
    q->qstats.size--;
    SIMPLEQ_REMOVE_HEAD(&q->queue, link);
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

double request_queue_get_average_size(struct request_queue_stats *last,
                                      struct request_queue_stats *current)
{
  int total_enqueued = current->total_enqueued - last->total_enqueued; 
  double size_sum = current->size_sum - last->size_sum; 
  return total_enqueued ? size_sum/total_enqueued : 0;
}

int request_queue_get_total_enqueued(struct request_queue_stats *last,
                                     struct request_queue_stats *current)
{
  return current->total_enqueued - last->total_enqueued; 
}

void request_queue_print_total_enqueued(struct request_queue_stats *last,
                                        struct request_queue_stats *current)
{
  int total_enqueued = request_queue_get_total_enqueued(last, current);
  printf("Total requests enqueued: %d\n", total_enqueued);
}

void request_queue_print_average_size(struct request_queue_stats *last,
                                      struct request_queue_stats *current)
{
  double average = request_queue_get_average_size(last, current);
  printf("Average request queue length: %lf\n", average);
}

