#include <malloc.h>
#include "request_queue.h"

void request_queue_init(struct request_queue *q, int request_size)
{
  q->request_size = request_size;

  q->total_enqueued = 0;
  q->size = 0;
  q->size_sum = 0;
  SIMPLEQ_INIT(&q->queue);
  spinlock_init(&q->lock);

  q->zombie_total_enqueued = 0;
  q->zombie_size = 0;
  q->zombie_size_sum = 0;
  SIMPLEQ_INIT(&q->zombie_queue);
  spinlock_init(&q->zombie_lock);
}

void *request_queue_create_request(struct request_queue *q)
{
  spinlock_lock(&q->zombie_lock);
  struct request *r = SIMPLEQ_FIRST(&q->zombie_queue);
  if(r) {
    q->zombie_size--;
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
  q->zombie_total_enqueued++;
  q->zombie_size++;
  q->zombie_size_sum += q->zombie_size;
  SIMPLEQ_INSERT_HEAD(&q->zombie_queue, r, link);
  spinlock_unlock(&q->zombie_lock);
}

void request_queue_enqueue_request(struct request_queue *q, struct request *r)
{
  spinlock_lock(&q->lock);
  q->size_sum += q->size;
  r->id = q->total_enqueued++;
  q->size++;
  SIMPLEQ_INSERT_TAIL(&q->queue, r, link);
  spinlock_unlock(&q->lock);
}

struct request *request_queue_dequeue_request(struct request_queue *q)
{
  spinlock_lock(&q->lock);
  struct request *r = SIMPLEQ_FIRST(&q->queue);
  if(r) {
    q->size--;
    SIMPLEQ_REMOVE_HEAD(&q->queue, link);
  }
  spinlock_unlock(&q->lock);
  return r;
}

