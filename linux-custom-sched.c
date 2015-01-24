#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <parlib/parlib.h>
#include <parlib/event.h>
#include <parlib/atomic.h>
#include <parlib/arch.h>
#include <parlib/vcore.h>
#include <parlib/mcs.h>
#include <parlib/alarm.h>
#include <parlib/timing.h>

#include "kweb.h"
/* including for the header values for faking support to the rest of kweb */
#include "tpool.h"
#include "kstats.h"

/******************************************************************************/
/* Stuff Kweb expects us to have */

struct kqueue				global_conns;
struct spin_pdr_lock 		gl_list_lock;
struct wthread_queue		gl_runnables;
struct spin_pdr_lock 		gl_zombie_lock;
struct wthread_queue		gl_zombies;
atomic_t					gl_total_threads;

/******************************************************************************/
/* Structs and helpers */

#define WTH_CREATED			1
#define WTH_RUNNABLE		2
#define WTH_RUNNING			3
#define WTH_ZOMBIE			4
#define WTH_BLK_YIELDING	5
#define WTH_BLK_SYSC		6	/* blocked on a syscall */
#define WTH_BLK_MUTEX		7	/* blocked externally, possibly on a mutex */
#define WTH_BLK_PAUSED		8	/* handed back to us from uthread code */

/* DEFINE_BCQ_TYPES(my_name, my_type, my_size);
 * struct my_name_bcq some_bcq;
 * bcq_init(&some_bcq, my_type, my_size);
 *
 * bcq_enqueue(&some_bcq, &some_my_type, my_size, num_fails_okay);
 * bcq_dequeue(&some_bcq, &some_my_type, my_size);
 */
struct wthread {
	struct uthread				uthread;
	TAILQ_ENTRY(wthread)		next;
	int							state;
	unsigned long				id;
	size_t						stacksize;
	void						*stacktop;
	void (*func)(void *, void *);
	void						*arg0;
	void						*arg1;
};
typedef struct wthread *wthread_t;

#define NEW_CONN_BCQ_SZ 		1024
DEFINE_BCQ_TYPES(new_conn, int, NEW_CONN_BCQ_SZ);

struct vc_mgmt
{
	struct kqueue				conns;
	struct wthread_queue		runnables;
	struct wthread_queue		zombies;

	unsigned long 				nr_total;
	unsigned long 				nr_runnables;
	unsigned long 				nr_zombies;
	unsigned long 				nr_blk_mutex;
	unsigned long 				nr_blk_sysc;

	uint64_t					total_idle_ticks;
	uint64_t					last_idle_ticks;
	bool						idle;				/* TODO consider flags */
	bool						tracking_idle_time;

	bool						accepting_conns;
	bool						should_yield;

	struct new_conn_bcq 		new_conns;			/* TODO cache align */
	unsigned int 				rseed;
}__attribute__((aligned(ARCH_CL_SIZE)));
struct vc_mgmt *vc_mgmt;

/* Linux Specific! (handle async syscall events) */
static void wth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type);

/* Wthread 2LS operations */
static void wth_sched_entry(void);
static void wth_thread_runnable(struct uthread *uthread);
static void wth_thread_paused(struct uthread *uthread);
static void wth_blockon_syscall(struct uthread *uthread, void *sysc);
static void wth_thread_has_blocked(struct uthread *uthread, int flags);

/* Wthread API */
void wthread_lib_init(void);
void wthread_exit(void);
struct wthread *wthread_self(void);
int __wthread_create(wthread_t *thread, void (*func)(void *, void *),
                     void *arg0, void *arg1);
int wthread_yield(void);

struct schedule_ops wthread_sched_ops = {
	wth_sched_entry,
	wth_thread_runnable,
	wth_thread_paused,
	wth_blockon_syscall,
	wth_thread_has_blocked,
	0, /* wth_preempt_pending, */
	0, /* wth_spawn_thread, */
};

/* Publish our sched_ops, overriding the weak defaults */
struct schedule_ops *sched_ops = &wthread_sched_ops;

static int get_next_vc(int i)
{
	return ++i % num_vcores();
}

// Warning, this will reuse numbers eventually
static unsigned long get_next_pid(void)
{
	static unsigned long next_pid = 0;
	return __sync_fetch_and_add(&next_pid, 1);
}

void os_init(void)
{
	kqueue_init(&global_conns, sizeof(struct http_connection));
	spin_pdr_init(&gl_list_lock);
	TAILQ_INIT(&gl_runnables);
	spin_pdr_init(&gl_zombie_lock);
	TAILQ_INIT(&gl_zombies);
	atomic_init(&gl_total_threads, 1);

	wthread_lib_init(); // 1 VC
	vcore_request(1);	/* one worker vcore, grow by instruction or on demand */
}

static int make_socket_non_blocking(int sfd)
{
  int flags, s;
  flags = fcntl (sfd, F_GETFL, 0);
  if(flags == -1) {
    perror("fcntl");
    return -1;
  }
  flags |= O_NONBLOCK;
  s = fcntl (sfd, F_SETFL, flags);
  if(s == -1) {
    perror("fcntl");
    return -1;
  }
  return 0;
}

void init_connection(struct http_connection *c)
{
	make_socket_non_blocking(c->socketfd);
}

void destroy_connection(struct http_connection *c)
{
}

ssize_t timed_read(struct http_connection *c, void *buf, size_t count)
{
  set_syscall_timeout(KWEB_SREAD_TIMEOUT * 1000);
  int ret = read(c->socketfd, buf, count);
  return ret;
}

ssize_t timed_write(struct http_connection *c, const void *buf, size_t count)
{
  int ret = 0;
  int remaining = count;
  while(remaining > 0) {
	set_syscall_timeout(KWEB_SREAD_TIMEOUT * 1000);
    ret = write(c->socketfd, buf, remaining);
    if(ret < 0)
      return ret;
    remaining -= ret;
  }
  return count;
}

static bool can_deliver_to(int vcoreid)
{
	/* could check if it is online or not too */
	struct vc_mgmt *vcm = &vc_mgmt[vcoreid];
	return vcm->accepting_conns;
}

void dispatch_call(int call_fd, void *client_addr)
{
	struct vc_mgmt *vcm_i;
	static int next_vc = 1;
	int num_vc = num_vcores();
	int i, j, fd = call_fd;

	for (i = next_vc, j = 0; j < num_vc; i = get_next_vc(i), j++) {
		if (!can_deliver_to(i))
			continue;
		vcm_i = &vc_mgmt[i];
		printd("Dispatching to VC %d\n", i);
		/* if vcores are yielding, we might need to confirm the target is still
		 * online */
		if (!bcq_enqueue(&vcm_i->new_conns, &fd, NEW_CONN_BCQ_SZ, 3)) {
			next_vc = get_next_vc(i);
			break;
		}
		printf("I am here\n");
	}
	if (j == num_vc) {
		printf("Failed to enqueue!\n");	// remove this later! XXX
		close(fd);	// XXX hangup/reset
	}
}

int yield_pcore(int pcoreid)
{
	int target = pcoreid;
	struct vc_mgmt *target_vcm = &vc_mgmt[target];
	target_vcm->should_yield = TRUE;
	send_event(NULL, EV_NONE, target);
	return 0;
}

/* Faking tpool and kstats */

void tpool_resize(struct tpool *t, int size)
{
	printf("Got resize request, skipping\n");
}

void tpool_inform_blocking(struct tpool *t)
{
}

void tpool_inform_unblocked(struct tpool *t)
{
}

void tpool_wake(struct tpool *t, int count)
{
}

void kstats_init(struct kstats *kstats, struct kqueue *kqueue,
                 struct tpool *tpool, struct cpu_util *cpu_util)
{
}

int kstats_start(struct kstats *kstats, unsigned int period_ms)
{
	return -1;
}

int kstats_stop(struct kstats *kstats)
{
	return -1;
}

void kstats_print_lifetime_statistics(struct kstats *kstats)
{
}

/* Debugging */
bool printx_on = FALSE;
#define printx(args...) if (printx_on) printf(args)

uint64_t idle_start_ticks = 0;
uint64_t idle_stop_ticks = 0;

static void start_idle_times(void)
{
	struct vc_mgmt *vcm_i;
	for (int i = 0; i < num_vcores(); i++) {
		vcm_i = &vc_mgmt[i];
		vcm_i->tracking_idle_time = TRUE;
	}
	idle_start_ticks = read_tsc();
}

static void stop_idle_times(void)
{
	struct vc_mgmt *vcm_i;
	for (int i = 0; i < num_vcores(); i++) {
		vcm_i = &vc_mgmt[i];
		vcm_i->tracking_idle_time = FALSE;
	}
	idle_stop_ticks = read_tsc();
}

static void reset_idle_times(void)
{
	struct vc_mgmt *vcm_i;
	for (int i = 0; i < num_vcores(); i++) {
		vcm_i = &vc_mgmt[i];
		vcm_i->total_idle_ticks = 0;	/* racy */
	}
	idle_start_ticks = read_tsc();	/* in case they reset while it's running */
}

/* TODO: header somewhere */
static void tsc2timespec(uint64_t tsc_time, struct timespec *ts)
{
	ts->tv_sec = tsc2sec(tsc_time);
	/* subtract off everything but the remainder */
	tsc_time -= sec2tsc(ts->tv_sec);
	ts->tv_nsec = tsc2nsec(tsc_time);
}

static void wth_handle_user_ipi(struct event_msg *ev_msg, unsigned int ev_type)
{
	struct vc_mgmt *vcm_i;
	uint64_t total_ticks;
	struct timespec ts;

	if (ev_msg) {
		switch (ev_msg->ev_arg1) {
			case (1):
				printx_on = TRUE;
				return;
			case (2):
				printx_on = FALSE;
				return;
			case (3):
				start_idle_times();
				return;
			case (4):
				stop_idle_times();
				return;
			case (5):
				reset_idle_times();
				return;
			default:
				break;
		}
	}

	/* if tracking idle time is still on, the diff is based on now */
	if (idle_start_ticks <= idle_stop_ticks)
		total_ticks = idle_stop_ticks - idle_start_ticks;
	else
		total_ticks = read_tsc() - idle_start_ticks;
	total_ticks = MAX(total_ticks, 1);	/* for the divide */
	tsc2timespec(total_ticks, &ts);
	printf("Total time %d.%06d\n", (int)ts.tv_sec, (int)ts.tv_nsec / 1000);
	for (int i = 0; i < num_vcores(); i++) {
		vcm_i = &vc_mgmt[i];
		/* these reads are racy */
		tsc2timespec(vcm_i->total_idle_ticks, &ts);
		printf("VC %2d: T %lu, R %lu, Z %lu, BM %lu, BS %lu, KQ %d, BCQ %d, "
		       "idle %d.%06d (%3d%%)\n",
		       i,
		       vcm_i->nr_total,
		       vcm_i->nr_runnables,
		       vcm_i->nr_zombies,
		       vcm_i->nr_blk_mutex,
		       vcm_i->nr_blk_sysc,
		       vcm_i->conns.qstats.size,
		       bcq_nr_full(&vcm_i->new_conns),
		       (int)ts.tv_sec, (int)(ts.tv_nsec / 1000),
		       (int)MIN((vcm_i->total_idle_ticks * 100) / total_ticks, 100));
	}

	// XXX G 
	unsigned long total = 0;
	unsigned long run = 0;
	unsigned long zom = 0;
	unsigned long bm = 0;
	unsigned long bs = 0;
	int gl_runq_len = 0;
	struct wthread *dummy;
	for (int i = 0; i < num_vcores(); i++) {
		vcm_i = &vc_mgmt[i];
		total += vcm_i->nr_total;
		run += vcm_i->nr_runnables;
		zom += vcm_i->nr_zombies;
		bm += vcm_i->nr_blk_mutex;
		bs += vcm_i->nr_blk_sysc;
	}
	spin_pdr_lock(&gl_list_lock);
	TAILQ_FOREACH(dummy, &gl_runnables, next)
		gl_runq_len++;
	spin_pdr_unlock(&gl_list_lock);

	printf("TOT %lu: T %lu, R %lu, Z %lu, BM %lu, BS %lu, KQ %d, GRQ %d\n",
	       atomic_read(&gl_total_threads), total, run, zom, bm, bs,
		   global_conns.qstats.size, gl_runq_len);
}

static struct wthread *__wthread_alloc(size_t stacksize)
{
	int offset = rand_r(&vc_mgmt[0].rseed) % max_vcores() * ARCH_CL_SIZE;
	stacksize += sizeof(struct wthread) + offset;
	stacksize = ROUNDUP(stacksize, PGSIZE);
	void *stackbot = mmap(
		0, stacksize, PROT_READ|PROT_WRITE|PROT_EXEC,
		MAP_PRIVATE|MAP_ANONYMOUS, -1, 0
	);
	if (stackbot == MAP_FAILED)
		abort();
	struct wthread *wthread = stackbot + stacksize
	                                - sizeof(struct wthread) - offset;
	wthread->stacktop = wthread;
	wthread->stacksize = stacksize - sizeof(struct wthread) - offset;
	return wthread;
}

static void __wthread_free(struct wthread *pt)
{
	assert(!munmap(pt->stacktop - pt->stacksize, pt->stacksize));
}

static void new_conv(int call_fd)
{
	struct kqueue *conns = &vc_mgmt[vcore_id()].conns;
	struct http_connection *c;
	
	c = kqueue_create_item(conns);	// allocation, could block
	c->burst_length = MAX_BURST;
	c->ref_count = 0;
	c->socketfd = call_fd;
	c->buf_length = 0;
	mutex_init(&c->writelock);
	c->should_close = 0;
	init_connection(c);
	printd("VC %d made conn from FD %d\n", vcore_id(), call_fd);
	enqueue_connection_tail(conns, c);
}

void __attribute__((noreturn)) wth_sched_entry(void)
{
	struct wthread *wth;
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	uint32_t vcoreid = vcore_id();
	struct kitem *next_conn;
	int new_conn_fd;

	/* TODO: could run older runnables instead */
	if (current_uthread) {
		printd("VC %d about to run %p\n", vcore_id(), current_uthread);
		run_current_uthread();
		assert(0);
	}

	if (vcoreid != 0)
		vcm->accepting_conns = TRUE;

	do {
		printd("VC %d about to handle events\n", vcore_id());
		/* handle events clears notif pending */
		handle_events(vcoreid);

		/* TODO: check for new connections, add them to old conn list */
		/* 10 is arbitrary... see notes above */

		while (vcm->conns.qstats.size < 10) {
			printd("VC %d trying to dequeue conns\n", vcore_id());
			if (!bcq_dequeue(&vcm->new_conns, &new_conn_fd, NEW_CONN_BCQ_SZ)) {
				new_conv(new_conn_fd);
			} else {
				break;
			}
		}

		/* TODO: sort by age?  minimum, running existing threads before making
		 * new ones for connections is a rough EDF. */
		wth = TAILQ_FIRST(&vcm->runnables);
		if (wth) {
			printd("VC %d got runnable %p\n", vcore_id(), wth);
			TAILQ_REMOVE(&vcm->runnables, wth, next);
			vcm->nr_runnables--;
			break;
		}

		/* 25 for 4 VC, 100 conn.  */
			// been doing 100 each recently.  change it up?
		if (vcm->nr_total - vcm->nr_zombies >= MAX_NR_THREADS)
			goto idle;

		/* TODO: check for new calls on existing/new connections */
		next_conn = kqueue_dequeue_item(&vcm->conns);
		if (next_conn) {
			if (__wthread_create(&wth, (void*)http_server, &vcm->conns,
			                     next_conn)) {
				// failed to make a thread (it panic'd already, deal with it)
				;
			}
			printd("VC %d made new thread %p for conn fd %d\n", vcore_id(), wth,
			       ((struct http_connection*)next_conn)->socketfd);
			wth->state = WTH_RUNNABLE;	/* bypassing rq */
			break;
		}
idle:
		if (vcm->should_yield) {
			/* TODO: need to drain our BCQ and kqueue (PVC2LS) */
			/* TODO: need to handle oustanding syscalls that might only come to
			 * our core (evq IPI stuff), see below. */
			vcm->accepting_conns = FALSE;
			/* might fail, if there was an event, etc.  not clear how to clean
			 * up, esp for the PVC case, though this is just a replacement for a
			 * real brain. */
			vcm->should_yield = FALSE; /* in case we come up naturally */
			vcore_yield(FALSE);
			/* could return on failure, and we want to try again.  might be some
			 * issues with this, if add_vcores happens before we yield. */
			vcm->should_yield = TRUE;
		}
		if (vcm->tracking_idle_time) {
			uint64_t now = read_tsc();
			/* only track if the previous loop was idle */
			if (vcm->idle)
				vcm->total_idle_ticks += now - vcm->last_idle_ticks;
			vcm->idle = TRUE;
			vcm->last_idle_ticks = now;
		}

		/* TODO: idle control */
		//cpu_relax();
	} while (1);

	vcm->idle = FALSE;

	assert(wth->state == WTH_RUNNABLE);
	/* Run the thread itself */
	run_uthread((struct uthread*)wth);
	assert(0);
}

static void __enqueue_head(struct vc_mgmt *vcm, struct wthread *wthread)
{
	TAILQ_INSERT_HEAD(&vcm->runnables, wthread, next);
}

static void __enqueue_tail(struct vc_mgmt *vcm, struct wthread *wthread)
{
	TAILQ_INSERT_TAIL(&vcm->runnables, wthread, next);
}

/* A common mistake with thread_runnable is to think it only runs in VC context.
 * Typically, it can run from uthread context, either after a create (as in
 * pthreads) or when mutexes unblock. */
void wth_thread_runnable(struct uthread *uthread)
{
	struct vc_mgmt *vcm;
	struct wthread *wthread = (struct wthread*)uthread;
	/* protect vcm state, could race with event handlers */
	uth_disable_notifs();
	cmb();
	vcm = &vc_mgmt[vcore_id()];
	/* At this point, the 2LS can see why the thread blocked and was woken up in
	 * the first place (coupling these things together).  On the yield path, the
	 * 2LS was involved and was able to set the state.  Now when we get the
	 * thread back, we can take a look. */
	printd("wthread %08p runnable, state was %d\n", wthread, wthread->state);
	switch (wthread->state) {
		case (WTH_CREATED):
		case (WTH_BLK_YIELDING):
		case (WTH_BLK_PAUSED):
			wthread->state = WTH_RUNNABLE;
			__enqueue_tail(vcm, wthread);
			break;
		case (WTH_BLK_SYSC):
			wthread->state = WTH_RUNNABLE;
#ifdef PREFER_UNBLOCKED_SYSC
			__enqueue_head(vcm, wthread);
#else
			__enqueue_tail(vcm, wthread);
#endif
			vcm->nr_blk_sysc--;
			break;
		case (WTH_BLK_MUTEX):
			wthread->state = WTH_RUNNABLE;
			__enqueue_tail(vcm, wthread);
			vcm->nr_blk_mutex--;
			break;
		default:
			printf("Odd state %d for wthread %p\n", wthread->state, wthread);
	}
	vcm->nr_runnables++;
	uth_enable_notifs();
	/* TODO: vc control */
}

/* For some reason not under its control, the uthread stopped running (compared
 * to yield, which was caused by uthread/2LS code).  Called from an event
 * handler, which might not return. */
void wth_thread_paused(struct uthread *uthread)
{
	struct wthread *wthread = (struct wthread*)uthread;
	/* communicate to wth_thread_runnable */
	wthread->state = WTH_BLK_PAUSED;
	/* At this point, you could do something clever, like put it at the front of
	 * the runqueue, see if it was holding a lock, do some accounting, or
	 * whatever. */
	wth_thread_runnable(uthread);
}

/* Restarts a uthread hanging off a syscall.  For the simple wthread case, we
 * just make it runnable and let the main scheduler code handle it. */
static void restart_thread(struct syscall *sysc)
{
	struct uthread *ut_restartee = (struct uthread*)sysc->u_data;
	assert(ut_restartee);
	assert(((struct wthread*)ut_restartee)->state == WTH_BLK_SYSC);
	assert(ut_restartee->sysc == sysc);	/* set in uthread.c */
	ut_restartee->sysc = 0;	/* so we don't 'reblock' on this later */
	wth_thread_runnable(ut_restartee);
}

static void wth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type)
{
	struct syscall *sysc;
	assert(in_vcore_context());
	/* if we just got a bit (not a msg), it should be because the process is
	 * still an SCP and hasn't started using the MCP ev_q yet (using the simple
	 * ev_q and glibc's blockon) or because the bit is still set from an old
	 * ev_q (blocking syscalls from before we could enter vcore ctx).  Either
	 * way, just return.  Note that if you screwed up the pth ev_q and made it
	 * NO_MSG, you'll never notice (we used to assert(ev_msg)). */
	if (!ev_msg)
		return;
	/* It's a bug if we don't have a msg (we're handling a syscall bit-event) */
	assert(ev_msg);
	/* Get the sysc from the message and just restart it */
	sysc = ev_msg->ev_arg3;
	assert(sysc);
	restart_thread(sysc);
}

/* This will be called from vcore context, after the current thread has yielded
 * and is trying to block on sysc.  Need to put it somewhere were we can wake it
 * up when the sysc is done.  For now, we'll have the kernel send us an event
 * when the syscall is done. */
void wth_blockon_syscall(struct uthread *uthread, void *syscall)
{
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	struct syscall *sysc = (struct syscall*)syscall;
	struct wthread *wthread = (struct wthread*)uthread;
	wthread->state = WTH_BLK_SYSC;

	/* Set things up so we can wake this thread up later */
	((struct syscall*)sysc)->u_data = uthread;
	vcm->nr_blk_sysc++;
}

void wth_thread_has_blocked(struct uthread *uthread, int flags)
{
	struct wthread *wthread = (struct wthread*)uthread;
	/* could imagine doing something with the flags.  For now, we just treat all
	 * externally blocked reasons as 'MUTEX'.  Whatever we do here, we are
	 * mostly communicating to our future selves in wth_thread_runnable(), which
	 * gets called by whoever triggered this callback */
	assert(0);
	wthread->state = WTH_BLK_MUTEX;
}

/* Down below: 
 *
void wth_thread_refl_fault(struct uthread *uthread, unsigned int trap_nr,
                           unsigned int err, unsigned long aux);
*/

void wth_preempt_pending(void)
{
}

void wth_spawn_thread(uintptr_t pc_start, void *data)
{
}


/******************************************************************************/
/* Wthread 2LS Helpers and API */

void wthread_lib_init(void)
{
	vcore_lib_init();
	vc_mgmt = parlib_aligned_alloc(PGSIZE,
	              sizeof(struct vc_mgmt) * max_vcores());
	assert(vc_mgmt);
	for (int i = 0; i < max_vcores(); i++) {
		kqueue_init(&vc_mgmt[i].conns, sizeof(struct http_connection));
		TAILQ_INIT(&vc_mgmt[i].runnables);
		TAILQ_INIT(&vc_mgmt[i].zombies);

		vc_mgmt[i].nr_total = 0;
		vc_mgmt[i].nr_runnables = 0;
		vc_mgmt[i].nr_zombies = 0;
		vc_mgmt[i].nr_blk_mutex = 0;
		vc_mgmt[i].nr_blk_sysc = 0;

		vc_mgmt[i].total_idle_ticks = 0;
		vc_mgmt[i].last_idle_ticks = 0;
		vc_mgmt[i].idle = FALSE;
		vc_mgmt[i].tracking_idle_time = FALSE;

		vc_mgmt[i].accepting_conns = FALSE;
		vc_mgmt[i].should_yield = FALSE;

		bcq_init(&vc_mgmt[i].new_conns, int, NEW_CONN_BCQ_SZ);
		vc_mgmt[i].rseed = i;
	}

	/* Create a wthread for the main thread */
	struct wthread *t = __wthread_alloc(0);
	t->id = get_next_pid();
	/* Fill in the main context stack info. */
	void *stackbottom;
	size_t stacksize;
	parlib_get_main_stack(&stackbottom, &stacksize);
	t->stacktop = stackbottom + stacksize;
	t->stacksize = stacksize;
	t->state = WTH_RUNNING;
	assert(t->id == 0);

	/* Handle syscall events. */
	/* These functions are declared in parlib for simulating async syscalls on linux */
	ev_handlers[EV_SYSCALL] = wth_handle_syscall;
	ev_handlers[EV_USER_IPI] = wth_handle_user_ipi;

	/* Initialize the uthread code (we're in _M mode after this).  Doing this
	 * last so that all the event stuff is ready when we're in _M mode.  Not a
	 * big deal one way or the other.  Note that vcore_init() hasn't happened
	 * yet, so if a 2LS somehow wants to have its init stuff use things like
	 * vcore stacks or TLSs, we'll need to change this. */
	uthread_lib_init((struct uthread*)t);
}

static void __wthread_run(void)
{
	struct wthread *me = wthread_self();

	me->func(me->arg0, me->arg1);

// XXX G  TDI
#ifdef TDI
	struct kitem *next_conn;
	/* i don't like this policy.  starts new while old might need work.  but
	 * with the yield it lets older stuff run.  saves a little on thread
	 * destruction? */
	while ((next_conn = kqueue_dequeue_item(me->arg0))) {
		me->func(me->arg0, next_conn);
#ifdef TDI_YIELDS
		wthread_yield(); // XXX G try dropping in and out
#endif
	}
#endif

	wthread_exit();
}

struct wthread *__wth_reanimate(void)
{
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	struct wthread *wth = TAILQ_FIRST(&vcm->zombies);
	if (!wth)
		return 0;
	TAILQ_REMOVE(&vcm->zombies, wth, next);
	vcm->nr_zombies--;
	/* consider 0'ing the uthread/wthread, at least flags and sysc. (want to
	 * keep stuff though, like the stack, id, etc)  */
	wth->state = WTH_CREATED;
	return wth;
}

int __wthread_create(wthread_t *thread, void (*func)(void *, void *),
                     void *arg0, void *arg1)
{
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];	/* called from VC ctx */
	/* Create the actual thread */
	struct wthread *wthread = __wth_reanimate();
	if (!wthread) {
		wthread = __wthread_alloc(KWEB_STACK_SZ);
		vcm->nr_total++;
		wthread->state = WTH_CREATED;
		wthread->id = get_next_pid();
	}
	wthread->func = func;
	wthread->arg0 = arg0;
	wthread->arg1 = arg1;
	init_uthread_tf(&wthread->uthread, __wthread_run,
	                wthread->stacktop - wthread->stacksize,
                    wthread->stacksize);
	uthread_init((struct uthread*)wthread);
	*thread = wthread;
	return 0;
}

void __wthread_generic_yield(struct wthread *wthread)
{
}

static void __wth_exit_cb(struct uthread *uthread, void *junk)
{
	struct wthread *wthread = (struct wthread*)uthread;
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];

	__wthread_generic_yield(wthread);
	wthread->state = WTH_ZOMBIE;
	printd("uth %p exiting\n", uthread);
	TAILQ_INSERT_HEAD(&vcm->zombies, wthread, next);
	vcm->nr_zombies++;
	atomic_decrement(&gl_total_threads);
}

/* In case we want to clean up */
static void wthread_destroy(struct wthread *wthread)
{
	struct uthread *uthread = (struct uthread*)wthread;
	uthread_cleanup(uthread);
	__wthread_free(wthread);
}

void wthread_exit(void)
{
	uthread_yield(FALSE, __wth_exit_cb, 0);
}

/* Callback/bottom half of yield.  For those writing these pth callbacks, the
 * minimum is call generic, set state (communicate with runnable), then do
 * something that causes it to be runnable in the future (or right now). */
static void __wth_yield_cb(struct uthread *uthread, void *junk)
{
	struct wthread *wthread = (struct wthread*)uthread;
	__wthread_generic_yield(wthread);
	wthread->state = WTH_BLK_YIELDING;
	/* just immediately restart it */
	wth_thread_runnable(uthread);
}

int wthread_yield(void)
{
	uthread_yield(TRUE, __wth_yield_cb, 0);
	return 0;
}

static void __wth_rutex_cb(struct uthread *uthread, void *junk)
{
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	struct wthread *wthread = (struct wthread*)uthread;
	__wthread_generic_yield(wthread);
	wthread->state = WTH_BLK_MUTEX;
	vcm->nr_blk_mutex++;
}

/* ghett racy mutex (per VC, no protection, for cooperative blocking where
 * threads don't migrate). */
void wthread_rutex_init(rutex_t *m)
{
	TAILQ_INIT(&m->waiters);
	m->in_use = FALSE;
}

void wthread_rutex_lock(rutex_t *m)
{
	while (m->in_use) {
		TAILQ_INSERT_TAIL(&m->waiters, wthread_self(), next);
		uthread_yield(TRUE, __wth_rutex_cb, 0);
		cmb();
	}
	m->in_use = TRUE;
}

void wthread_rutex_unlock(rutex_t *m)
{
	struct wthread *waiter = TAILQ_FIRST(&m->waiters);

	m->in_use = FALSE;
	if (waiter) {
		/* FIFO, might want something else */
		TAILQ_REMOVE(&m->waiters, waiter, next);
		wth_thread_runnable((struct uthread*)waiter);
	}
}

// XXX G 1 for mutex, 0 brutex, change header
#ifndef BRUTEX
/* non-racy versions, usable cross-core */
static void __wth_mutex_cb(struct uthread *uthread, void *mutex)
{
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	struct wthread *wthread = (struct wthread*)uthread;
	mutex_t *m = (mutex_t*)mutex;

	__wthread_generic_yield(wthread);
	wthread->state = WTH_BLK_MUTEX;
	vcm->nr_blk_mutex++;

	spin_pdr_unlock(&m->lock);
}

void wthread_mutex_init(mutex_t *m)
{
	TAILQ_INIT(&m->waiters);
	spin_pdr_init(&m->lock);
	m->in_use = FALSE;
}

void wthread_mutex_lock(mutex_t *m)
{
	spin_pdr_lock(&m->lock); // disable depth might get messed up
	if (!m->in_use) {
		m->in_use = TRUE;
		spin_pdr_unlock(&m->lock);
		return;
	}
	TAILQ_INSERT_TAIL(&m->waiters, wthread_self(), next);
	uthread_yield(TRUE, __wth_mutex_cb, m);
}

void wthread_mutex_unlock(mutex_t *m)
{
	struct wthread *waiter;
	
	spin_pdr_lock(&m->lock);
	waiter = TAILQ_FIRST(&m->waiters);
	if (!waiter) {
		m->in_use = FALSE;
		spin_pdr_unlock(&m->lock);
	} else {
		/* FIFO, might want something else */
		TAILQ_REMOVE(&m->waiters, waiter, next);
		/* in_use is still TRUE, passing the lock off, hoare style */
		spin_pdr_unlock(&m->lock);
		wth_thread_runnable((struct uthread*)waiter);
	}
}

#else // brutex

// XXX G
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun)
{
	if ((*spun)++ == spins) {
		wthread_yield();
		*spun = 0;
	}
}

void wthread_brutex_init(brutex_t *m)
{
	atomic_init(&m->lock, 0);
}

int wthread_brutex_trylock(brutex_t *m)
{
	return atomic_swap(&m->lock, 1) == 0 ? 0 : EBUSY;
}

void wthread_brutex_lock(brutex_t *m)
{
	unsigned int spinner = 0;
	while (wthread_brutex_trylock(m))
		while (*(volatile size_t*)&m->lock) {
			cpu_relax();
			// XXX G
#ifdef BRUTEX_YIELD_IMMEDIATELY
			wthread_yield();
#else
			spin_to_sleep(100, &spinner);
#endif
		}
	/* normally we'd need a wmb() and a wrmb() after locking, but the
	 * atomic_swap handles the CPU mb(), so just a cmb() is necessary. */
	cmb();
}

void wthread_brutex_unlock(brutex_t* m)
{
	/* keep reads and writes inside the protected region */
	rwmb();
	wmb();
	atomic_set(&m->lock, 0);
}

#endif


struct wthread *wthread_self(void)
{
	return (struct wthread*)current_uthread;
}

