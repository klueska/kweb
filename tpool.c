#include <malloc.h>
#include "tpool.h"
#include "os.h"

void request_queue_init(struct request_queue *q, 
                        void (*func)(struct request_queue *, struct request *),
                        int request_size)
{
  q->request_size = request_size;
  q->func = func;

  q->total_enqueued = 0;
  q->size = 0;
  q->size_sum = 0;
  STAILQ_INIT(&q->queue);
  spinlock_init(&q->lock);

  q->zombie_total_enqueued = 0;
  q->zombie_size = 0;
  q->zombie_size_sum = 0;
  STAILQ_INIT(&q->zombie_queue);
  spinlock_init(&q->zombie_lock);
}

void *create_request(struct request_queue *q)
{
  spinlock_lock(&q->zombie_lock);
  struct request *r = STAILQ_FIRST(&q->zombie_queue);
  if(r) {
    q->zombie_size--;
    STAILQ_REMOVE_HEAD(&q->zombie_queue, link);
  }
  spinlock_unlock(&q->zombie_lock);

  if(r == NULL) {
    r = malloc(q->request_size);
  }
  r->id = 0;
  return r;
}

void destroy_request(struct request_queue *q, struct request *r)
{
  spinlock_lock(&q->zombie_lock);
  q->zombie_total_enqueued++;
  q->zombie_size++;
  q->zombie_size_sum += q->zombie_size;
  STAILQ_INSERT_HEAD(&q->zombie_queue, r, link);
  spinlock_unlock(&q->zombie_lock);
}

void enqueue_request(struct request_queue *q, struct request *r)
{
  spinlock_lock(&q->lock);
  r->id = q->total_enqueued++;
  q->size++;
  q->size_sum += q->size;
  STAILQ_INSERT_HEAD(&q->queue, r, link);
  spinlock_unlock(&q->lock);

  futex_wake(&q->total_enqueued, 1);
}

static struct request *__dequeue_request(struct request_queue *q)
{
  struct request *r = STAILQ_FIRST(&q->queue);
  if(r) {
    q->size--;
    STAILQ_REMOVE_HEAD(&q->queue, link);
  }
  return r;
}

static void *__thread_wrapper(void *arg)
{
  int total_enqueued = 0;
  struct request_queue *q = (struct request_queue*)arg;
  while(1) {
    spinlock_lock(&q->lock);
    struct request *r = __dequeue_request(q);
    if(r == NULL) {
      total_enqueued = q->total_enqueued;
    }
    spinlock_unlock(&q->lock);

    if(r) {
      q->func(q, r);
    }
    else {
      futex_wait(&q->total_enqueued, total_enqueued);
    }
  }
}

int tpool_init(struct request_queue *q, int num)
{
  int num_created = 0;
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, TPOOL_STACK_SZ);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  for(int i=0; i<num; i++) {
    if(pthread_create(&thread, &attr, __thread_wrapper, q) == 0)
      num_created++;
    else
      break;
  }
  return num_created;
}

