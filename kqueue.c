#include <malloc.h>
#include "kqueue.h"
#include "tsc.h"

void kqueue_init(struct kqueue *q, int item_size)
{
  q->item_size = item_size;
  q->ids = 0;

  STAILQ_INIT(&q->queue);
  spin_pdr_init(&q->lock);
  q->qstats.size = 0;
  q->qstats.size_sum = 0;
  q->qstats.total_enqueued = 0;
  q->qstats.total_dequeued = 0;
  q->qstats.wait_time_sum = 0;

  STAILQ_INIT(&q->zombie_queue);
  spin_pdr_init(&q->zombie_lock);
  q->zombie_qstats.size = 0;
  q->zombie_qstats.size_sum = 0;
  q->zombie_qstats.total_enqueued = 0;
  q->zombie_qstats.total_dequeued = 0;
  q->zombie_qstats.wait_time_sum = 0;
}

void *kqueue_create_item(struct kqueue *q)
{
  spin_pdr_lock(&q->zombie_lock);
  struct kitem *r = STAILQ_FIRST(&q->zombie_queue);
  if(r) {
    STAILQ_REMOVE_HEAD(&q->zombie_queue, link);
    r->dequeue_time = read_tsc();
    q->zombie_qstats.total_dequeued++;
    q->zombie_qstats.wait_time_sum += (r->dequeue_time - r->enqueue_time);
    q->zombie_qstats.size--;
  }
  spin_pdr_unlock(&q->zombie_lock);

  if(r == NULL) {
    r = malloc(q->item_size);
  }
  r->id = q->ids++;
  r->enqueue_time = 0;
  r->dequeue_time = 0;
  return r;
}

void kqueue_destroy_item(struct kqueue *q, struct kitem *r)
{
  spin_pdr_lock(&q->zombie_lock);
  q->zombie_qstats.size_sum += q->zombie_qstats.size++;
  q->zombie_qstats.total_enqueued++;
  r->enqueue_time = read_tsc();
  STAILQ_INSERT_HEAD(&q->zombie_queue, r, link);
  spin_pdr_unlock(&q->zombie_lock);
}

void kqueue_enqueue_item_head(struct kqueue *q, struct kitem *r)
{
  spin_pdr_lock(&q->lock);
  q->qstats.size_sum += q->qstats.size++;
  q->qstats.total_enqueued++;
  r->enqueue_time = read_tsc();
  STAILQ_INSERT_HEAD(&q->queue, r, link);
  spin_pdr_unlock(&q->lock);
}

void kqueue_enqueue_item_tail(struct kqueue *q, struct kitem *r)
{
  spin_pdr_lock(&q->lock);
  q->qstats.size_sum += q->qstats.size++;
  q->qstats.total_enqueued++;
  r->enqueue_time = read_tsc();
  STAILQ_INSERT_TAIL(&q->queue, r, link);
  spin_pdr_unlock(&q->lock);
}

struct kitem *kqueue_dequeue_item(struct kqueue *q)
{
  spin_pdr_lock(&q->lock);
  struct kitem *r = STAILQ_FIRST(&q->queue);
  if(r) {
    STAILQ_REMOVE_HEAD(&q->queue, link);
    r->dequeue_time = read_tsc();
    q->qstats.total_dequeued++;
    q->qstats.wait_time_sum += (r->dequeue_time - r->enqueue_time);
    q->qstats.size--;
  }
  spin_pdr_unlock(&q->lock);
  return r;
}

struct kqueue_stats kqueue_get_stats(struct kqueue *q)
{
  spin_pdr_lock(&q->lock);
  struct kqueue_stats s = q->qstats;
  spin_pdr_unlock(&q->lock);
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

void kqueue_print_total_enqueued(char *prefix,
                                 struct kqueue_stats *prev,
                                 struct kqueue_stats *curr)
{
  int total_enqueued = kqueue_get_total_enqueued(prev, curr);
  printf("%sTotal items enqueued: %d\n", prefix, total_enqueued);
}

void kqueue_print_average_size(char *prefix,
                               struct kqueue_stats *prev,
                               struct kqueue_stats *curr)
{
  double average = kqueue_get_average_size(prev, curr);
  printf("%sAverage kqueue length: %lf\n", prefix, average);
}

void kqueue_print_average_wait_time(char *prefix,
                                    struct kqueue_stats *prev,
                                    struct kqueue_stats *curr)
{
  double average = kqueue_get_average_wait_time(prev, curr);
  printf("%sAverage wait time in kqueue: %lf\n", prefix, average);
}

