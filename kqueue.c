#include <malloc.h>
#include "kqueue.h"
#include "tsc.h"

void kqueue_init(struct kqueue *q, int item_size)
{
  q->item_size = item_size;

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

void *kqueue_create_item(struct kqueue *q)
{
  spinlock_lock(&q->zombie_lock);
  struct kitem *r = SIMPLEQ_FIRST(&q->zombie_queue);
  if(r) {
    SIMPLEQ_REMOVE_HEAD(&q->zombie_queue, link);
    r->dequeue_time = read_tsc();
    q->zombie_qstats.total_dequeued++;
    q->zombie_qstats.wait_time_sum += (r->dequeue_time - r->enqueue_time);
    q->zombie_qstats.size--;
  }
  spinlock_unlock(&q->zombie_lock);

  if(r == NULL) {
    r = malloc(q->item_size);
  }
  r->id = 0;
  r->enqueue_time = 0;
  r->dequeue_time = 0;
  return r;
}

void kqueue_destroy_item(struct kqueue *q, struct kitem *r)
{
  spinlock_lock(&q->zombie_lock);
  q->zombie_qstats.size_sum += q->zombie_qstats.size++;
  r->id = q->zombie_qstats.total_enqueued++;
  r->enqueue_time = read_tsc();
  SIMPLEQ_INSERT_HEAD(&q->zombie_queue, r, link);
  spinlock_unlock(&q->zombie_lock);
}

void kqueue_enqueue_item(struct kqueue *q, struct kitem *r)
{
  spinlock_lock(&q->lock);
  q->qstats.size_sum += q->qstats.size++;
  r->id = q->qstats.total_enqueued++;
  r->enqueue_time = read_tsc();
  SIMPLEQ_INSERT_TAIL(&q->queue, r, link);
  spinlock_unlock(&q->lock);
}

struct kitem *kqueue_dequeue_item(struct kqueue *q)
{
  spinlock_lock(&q->lock);
  struct kitem *r = SIMPLEQ_FIRST(&q->queue);
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

struct kqueue_stats kqueue_get_stats(struct kqueue *q)
{
  spinlock_lock(&q->lock);
  struct kqueue_stats s = q->qstats;
  spinlock_unlock(&q->lock);
  return s;
}

double kqueue_get_average_size(struct kqueue_stats *prev,
                               struct kqueue_stats *curr)
{
  int total_enqueued = curr->total_enqueued - prev->total_enqueued; 
  double size_sum = curr->size_sum - prev->size_sum; 
  return total_enqueued ? size_sum/total_enqueued : 0;
}

int kqueue_get_total_enqueued(struct kqueue_stats *prev,
                              struct kqueue_stats *curr)
{
  return curr->total_enqueued - prev->total_enqueued; 
}

double kqueue_get_average_wait_time(struct kqueue_stats *prev,
                                    struct kqueue_stats *curr)
{
  double wait_time_sum = curr->wait_time_sum - prev->wait_time_sum;
  uint64_t total_dequeued = curr->total_dequeued - prev->total_dequeued;
  return total_dequeued ? wait_time_sum/total_dequeued : 0;
}

void kqueue_print_total_enqueued(struct kqueue_stats *prev,
                                 struct kqueue_stats *curr)
{
  int total_enqueued = kqueue_get_total_enqueued(prev, curr);
  printf("Total items enqueued: %d\n", total_enqueued);
}

void kqueue_print_average_size(struct kqueue_stats *prev,
                               struct kqueue_stats *curr)
{
  double average = kqueue_get_average_size(prev, curr);
  printf("Average kqueue length: %lf\n", average);
}

void kqueue_print_average_wait_time(struct kqueue_stats *prev,
                                    struct kqueue_stats *curr)
{
  double average = kqueue_get_average_wait_time(prev, curr);
  printf("Average wait time in kqueue: %lf\n", average);
}

