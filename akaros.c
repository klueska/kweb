/* sched entry notes:
 *
 * which first?  kweb basically accepts as fast as possible, and puts
 * them on the end of the queue.  never really pushes back.
 * 		this is a fundamental problem, but the networking stack also
 * 		pushes back (in its own, arguably shitty way)
 *
 * the "10 burst, then back of the line" approach means we need to pull
 * off new conns while processing old conns (so there is a line to put
 * them in back of).
 * 		if we do this, then the 2LS should look at the kqueue length,
 * 		not the BCQ or whatever
 *
 * 		if so, should we do this before handling the runnables?  the
 * 		runnables involve re-enqueueing an item. (any thread can
 * 		requeue)
 *
 *		could base whether or not we get a new conn based on the kqueue
 *		length.  normally, we would only pull new conns when we have
 *		free time.  now we need a fake param
 *
 *		or we could just support up to 100 bursts, front of the line, then reset
 *		(like apache).  we don't need to accept until we're out of existing
 *		connections.
 *
 *		incidentally, this policy treats calls after a max burst as a new
 *		connection, in terms of priority and whatnot.  unless we accept
 *		'everything' instantly, it won't be ordered after everything. 
 *
 *		and if we do order after everything (currently accepted), then the later
 *		elements of the burst will get worse treatment.  you'd see steps in the
 *		response time, max_burst wide.
 *			do we see any artifacts of this yet?  with massive bursts, what does
 *			response time look like?  (i guess httperf doesn't get to put them
 *			in and start the clock til the queues unclog a little.  would this
 *			be at the joint of the latency curve?  (somehow we are offering
 *			exactly our max throughput, and nothing more?)
 *
 *		also, with this current style (multiple threads per connection), when a
 *		thread is done with one call, it exits.  then gets reanimated to finish
 *		the conn.
 *
 * if we're handling multiple connections on one core and if we FIFO the
 * runnables, then does intra-conn concurrency even matter?
 *    - concurrency, not parallelism
 *    - once a connection unlocks, a thread from that connection might
 *    not run immediately.
 *    	- change the semaphore to insert at the head?
 *    	- should still be an EDF policy
 *    - the idea was to use multiple threads to speed up the http part,
 *    so that the conv/socket is being used as much as possible.
 *    	- on multicore conns, we can actually parallelize it
 *    	- per-VC conns, we just get a little I/O concurrency, at a cost
 *    	of more threads (x * nr_bursts)
 *    	
 *
 * i didn't want to have the accepter push them directly onto the vc
 * queue, contention/parallelism reasons.  also, it matches a per-core
 * accept style better.  similarly, there isn't a global accept queue (could
 * have one big BCQ), since that won't match the NIC lane stuff.
 *
 * for tail latency, we might want to put long runners at the end of the q.
 * ordered after everything as of a certain time.  which makes me want to accept
 * things faster, so that i know their time in system. (clock already started,
 * the 2LS just doesn't know it yet).  OTOH, i'd rather reject early if we're
 * overloaded, so as to not waste effort.
 *
 * extract_request: browsers will leave connections open for a while (try
 * loading a page with FF, then loading it again.  it'll reuse the connection,
 * and the wthread will return from blocking in extract_request.)
 *
 *
 * multiple sources of queueing, multiple places where we accept everything
 * 		accept loop, never pushes back.  the 9ns stack does eventually
 * 			can start closing/hanging up if the VC BCQs are full
 *
 * 		VC BCQs: don't drain if we have more than 10 connections, trying to push
 * 		back
 * 		
 * 		but with massive bursts, we can have an unlimited amount of threads per
 * 		connection
 * 			VC dequeues, creates thread, it reenqueues, then blocks.
 * 			if we block for a while, we'd do that over and over
 * 			- just draining lots of calls from a queue, putting them in a longer
 * 			queue (waiting on the connection mutex).
 * 			- in the time it takes to send out a response, are we adding new
 * 			threads?
 *
 * 		maybe there should be a limit on the number of threads we allow for any
 * 		connection  (definitely!!!)
 * 			what do we do when we hit this point?  kill the connection?
 * 			didn't old kweb also do this?  why didn't that happen?
 *
 *
 * 	thread migration: if events are handled via fallback, we're a little
 * 	fucked.  preemption would screw us too.
 * 		main thing is the assumption that threads of the same connection execute
 * 		on the same vcore (the rutex).
 *
 * 		that and stats (block on one, unblock on another)
 *
 * 		also, wth_thread_runnable assumes notifs disabled
 * 			as with pth_, it can run in uth ctx, like from rutex_unlock
 *
 * 	incidentally, there is a long gap of time before any syscalls complete.  100
 * 	connections dispatched, long delay, then ~15 sysc completes.  then a flood of
 * 	completes.
 *
 * TODO:
 * 		threading pushback
 * 			can't just accept everything...
 * 			hasn't been a problem yet.  can uncomment the < 500 bit
 * 		
 * 		vc control: when to get more and when to yield (hardcode for now)
 *
 * 		alarm cancelling: sucks contending on a global lock for every timed
 * 		write.
 * 			didn't have a huge effect
 */

#define _GNU_SOURCE
#include <ros/trapframe.h>
#include <vcore.h>
#include <mcs.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <parlib.h>
#include <ros/event.h>
#include <arch/atomic.h>
#include <arch/arch.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <event.h>
#include <ucq.h>
#include <signal.h>
#include <unistd.h>
#include <alarm.h>
#include <ros/bcq.h>

#include "kweb.h"
/* including for the header values for faking support to the rest of kweb */
#include "tpool.h"
#include "kstats.h"

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

	sigset_t					sigmask;
	sigset_t					sigpending;
	struct sigdata				*sigdata;
};
typedef struct wthread *wthread_t;

#define NEW_CONN_BCQ_SZ 		16
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

	struct new_conn_bcq 		new_conns;			/* TODO cache align */
	struct event_queue			*ev_q;
}__attribute__((aligned(ARCH_CL_SIZE)));

struct vc_mgmt *vc_mgmt;

static void __wthread_free_stack(struct wthread *pt);
static int __wthread_allocate_stack(struct wthread *pt);
static void __wth_yield_cb(struct uthread *uthread, void *junk);
static void __wthread_prep_for_pending_posix_signals(wthread_t wthread);
void wthread_lib_init(void);
void wthread_exit(void);
struct wthread *wthread_self(void);
int __wthread_create(wthread_t *thread, void (*func)(void *, void *),
                     void *arg0, void *arg1);

/******************************************************************************/
/* Stuff Kweb expects us to have */

void os_init(void)
{
	wthread_lib_init(); // 1 VC
	vcore_request(3); // XXX 0 + enough for 1 non-hype worker (4 total)
	//vcore_request(1);	/* one worker vcore, grow by instruction or on demand */
}

/* only called by ktimer, which isn't called */
int usleep(useconds_t usec)
{
	sys_block(usec);
	/* TODO: should check for syscall abortion and error out.  man usleep */
	return 0;
}

void init_connection(struct http_connection *c)
{
}

void destroy_connection(struct http_connection *c)
{
}

ssize_t timed_read(struct http_connection *c, void *buf, size_t count)
{
	ssize_t ret;
	struct alarm_waiter waiter;

	init_awaiter(&waiter, alarm_abort_sysc);
	waiter.data = current_uthread;

	set_awaiter_rel(&waiter, KWEB_SREAD_TIMEOUT * 1000);
	//set_alarm(&waiter);

	tpool_inform_blocking(&tpool);
	ret = read(c->socketfd, buf, count);
	tpool_inform_unblocked(&tpool);

	//unset_alarm(&waiter);
	return ret;
}

ssize_t timed_write(struct http_connection *c, const void *buf, size_t count)
{
	ssize_t ret;
	struct alarm_waiter waiter;

	init_awaiter(&waiter, alarm_abort_sysc);
	waiter.data = current_uthread;

	set_awaiter_rel(&waiter, KWEB_SREAD_TIMEOUT * 1000);
	//set_alarm(&waiter);

	tpool_inform_blocking(&tpool);
	ret = write(c->socketfd, buf, count);
	tpool_inform_unblocked(&tpool);

	//unset_alarm(&waiter);
	return ret;
}

static int get_next_vc(int i)
{
	return ++i % num_vcores();
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
	}
	if (j == num_vc) {
		printf("Failed to enqueue!\n");	// remove this later! XXX
		close(fd);	// XXX hangup/reset
	}
	/* TODO vcore control */

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


/******************************************************************************/
/* Wthread 2LS operations */

void wth_sched_entry(void);
void wth_thread_runnable(struct uthread *uthread);
void wth_thread_paused(struct uthread *uthread);
void wth_thread_blockon_sysc(struct uthread *uthread, void *sysc);
void wth_thread_has_blocked(struct uthread *uthread, int flags);
void wth_thread_refl_fault(struct uthread *uthread, unsigned int trap_nr,
                           unsigned int err, unsigned long aux);
void wth_preempt_pending(void);
void wth_spawn_thread(uintptr_t pc_start, void *data);

static void wth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type,
                               void *data);

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

static void wth_handle_user_ipi(struct event_msg *ev_msg, unsigned int ev_type,
                                void *data)
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
	printf("Total time %d.%06d\n", ts.tv_sec, ts.tv_nsec / 1000);
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
		       ts.tv_sec, ts.tv_nsec / 1000,
		       MIN((vcm_i->total_idle_ticks * 100) / total_ticks, 100));
	}
}

struct schedule_ops wthread_sched_ops = {
	wth_sched_entry,
	wth_thread_runnable,
	wth_thread_paused,
	wth_thread_blockon_sysc,
	wth_thread_has_blocked,
	wth_thread_refl_fault,
	0, /* wth_preempt_pending, */
	0, /* wth_spawn_thread, */
};

/* Publish our sched_ops, overriding the weak defaults */
struct schedule_ops *sched_ops = &wthread_sched_ops;

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
	init_connection(c);
	printd("VC %d made conn from FD %d\n", vcore_id(), call_fd);
	enqueue_connection_tail(conns, c);
}

static void hyperthreading_hacks(void)
{
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	uint32_t vcoreid = vcore_id();

// ugh, all the num_vc shit assumes the vcorelist is packed.
// we actually want "max vc id" (max i ever asked for)

// need a syscall for the vcore swapping
// 	sys_swap_vcores(int vc1, int vc2)
// 	both must be mapped
// 	preempted both
// 	definitely change the mapping
// 	startcore flipped
// 		startcore or change_to helper?
// 	don't send messages
// 	don't muck with lists
// 	maybe muck with times (prob not)

// if we do a swap, say from 0 to 1 ( 0 figures out who is on an even pcore and
// takes it, pref pcore2).  when 1 starts up, it's pcoreid will have changed.
// it might now be supposed to halt.  so i guess we'll need to do the hack check
// in the event loop?
//
// instead of the swap, we could handoff the dispatcher.  and the accepter
// thread.
//
	/* ugh... */
	if (vcoreid == 0) {
		while (__procinfo.vcoremap[vcoreid].pcoreid != 2) {
			vcore_yield(FALSE);
			/* hacky yield too, we have no way to guarantee someone else will
			 * change to us */
			cmb();
		}
		/* ugh, this doesn't give us back pcore 1.  now we have a hole again */
		// 9 is fucked too.  that depends on how many evens vs odds
		while (__procdata.res_req[RES_CORES].amt_wanted < 4)
			vcore_request(1);

	} else {
		while (__procinfo.vcoremap[vcoreid].pcoreid == 2) {
			vcm->accepting_conns = FALSE;
			/* will not return on success */
			sys_change_vcore(0, TRUE);
			cmb();
		}
		while (__procinfo.vcoremap[vcoreid].pcoreid % 2 == 1) {
			vcm->accepting_conns = FALSE;
			/* will not return (as of current hacks) */
				// wrong, an IPI will break out of the halt, we'll return.  we
				// just might not get a notif.  also, we pretend like we're a
				// uthread, so if the kernel does decide to preempt or notify
				// us, it'll put our old ctx into the uth slot (should be okay)
			// XXX also, this doesn't handle kernel states yet
			vcore_idle();
			cmb();
		}
	}
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
		__wthread_prep_for_pending_posix_signals((wthread_t)current_uthread);
		run_current_uthread();
		assert(0);
	}

	hyperthreading_hacks();
	if (vcoreid != 0)
		vcm->accepting_conns = TRUE;

	do {
		printd("VC %d about to handle events\n", vcore_id());
		handle_events(vcoreid);
		__check_preempt_pending(vcoreid);

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
		if (vcm->nr_total - vcm->nr_zombies > 100)
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
	__wthread_prep_for_pending_posix_signals(wth);
	/* Run the thread itself */
	run_uthread((struct uthread*)wth);
	assert(0);
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
			break;
		case (WTH_BLK_SYSC):
			vcm->nr_blk_sysc--;
			break;
		case (WTH_BLK_MUTEX):
			vcm->nr_blk_mutex--;
			break;
		default:
			printf("Odd state %d for wthread %08p\n", wthread->state, wthread);
	}
	wthread->state = WTH_RUNNABLE;
	TAILQ_INSERT_TAIL(&vcm->runnables, wthread, next);
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

static void wth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type,
                               void *data)
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
void wth_thread_blockon_sysc(struct uthread *uthread, void *syscall)
{
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	struct syscall *sysc = (struct syscall*)syscall;
	int old_flags;
	struct wthread *wthread = (struct wthread*)uthread;
	wthread->state = WTH_BLK_SYSC;
	/* Set things up so we can wake this thread up later */
	sysc->u_data = uthread;
	vcm->nr_blk_sysc++;
	/* Register our vcore's syscall ev_q to hear about this syscall. */
	if (!register_evq(sysc, vcm->ev_q)) {
		/* Lost the race with the call being done.  The kernel won't send the
		 * event.  Just restart him. */
		restart_thread(sysc);
	}
	/* GIANT WARNING: do not touch the thread after this point. */
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

static void __wthread_free_stack(struct wthread *pt)
{
	int ret = munmap(pt->stacktop - pt->stacksize, pt->stacksize);
	assert(!ret);
}

static int __wthread_allocate_stack(struct wthread *pt)
{
	assert(pt->stacksize);
	void* stackbot = mmap(0, pt->stacksize,
	                      PROT_READ|PROT_WRITE|PROT_EXEC,
	                      MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
	if (stackbot == MAP_FAILED)
		return -1; // errno set by mmap
	pt->stacktop = stackbot + pt->stacksize;
	return 0;
}

static unsigned long get_next_pid(void)
{
	static unsigned long next_pid = 0;
	return __sync_fetch_and_add(&next_pid, 1);
}

void wthread_lib_init(void)
{
	uintptr_t mmap_block;
	struct wthread *t;
	int ret;

	assert(!in_multi_mode());
	/* Create a wthread for the main thread */
	ret = posix_memalign((void**)&t, __alignof__(struct wthread),
	                     sizeof(struct wthread));
	assert(!ret);
	memset(t, 0, sizeof(struct wthread));	/* aggressively 0 for bugs */
	t->id = get_next_pid();
	t->stacksize = USTACK_NUM_PAGES * PGSIZE;
	t->stacktop = (void*)USTACKTOP;
	t->state = WTH_RUNNING;
	__sigemptyset(&t->sigmask);
	__sigemptyset(&t->sigpending);
	assert(t->id == 0);

	vc_mgmt = malloc(sizeof(struct vc_mgmt) * max_vcores());
	assert(vc_mgmt);
	mmap_block = (uintptr_t)mmap(0, PGSIZE * 2 * max_vcores(),
	                             PROT_WRITE | PROT_READ,
	                             MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	assert(mmap_block);
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

		bcq_init(&vc_mgmt[i].new_conns, int, NEW_CONN_BCQ_SZ);
#if 0
		/* TODO: IPI, for now.  might poll later.  indir is a toss-up too.
		 * though the current sched assumes threads don't migrate. */
		vc_mgmt[i].ev_q = get_big_event_q_raw();
		vc_mgmt[i].ev_q->ev_flags = EVENT_IPI | EVENT_INDIR | EVENT_FALLBACK;
		vc_mgmt[i].ev_q->ev_vcore = i;
		ucq_init_raw(&vc_mgmt[i].ev_q->ev_mbox->ev_msgs, 
		             mmap_block + (2 * i    ) * PGSIZE, 
		             mmap_block + (2 * i + 1) * PGSIZE); 
#else
		/* public vcpd mbox: will poll when we handle_events() */
		vc_mgmt[i].ev_q = get_event_q_vcpd(i, 0);
		vc_mgmt[i].ev_q->ev_flags = 0;
#endif

	}
	register_ev_handler(EV_SYSCALL, wth_handle_syscall, 0);
	register_ev_handler(EV_USER_IPI, wth_handle_user_ipi, 0);
	enable_kevent(EV_USER_IPI, 0, EVENT_IPI | EVENT_VCORE_PRIVATE);
	uthread_lib_init((struct uthread*)t);
}

static void __wthread_run(void)
{
	struct wthread *me = wthread_self();
	me->func(me->arg0, me->arg1);
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
	struct uth_thread_attr uth_attr = {0};
	/* Create the actual thread */
	struct wthread *wthread = __wth_reanimate();
	int ret;
	if (!wthread) {
		ret = posix_memalign((void**)&wthread, __alignof__(struct wthread),
		                     sizeof(struct wthread));
		assert(!ret);
		vcm->nr_total++;
		memset(wthread, 0, sizeof(struct wthread));	/* aggressively 0 for bugs*/
		wthread->stacksize = KWEB_STACK_SZ;
		wthread->state = WTH_CREATED;
		wthread->id = get_next_pid();

		sigfillset(&wthread->sigmask);
		sigemptyset(&wthread->sigpending);
		wthread->sigdata = NULL;

		if (__wthread_allocate_stack(wthread))
			printf("We're fucked\n");
	}
	wthread->func = func;
	wthread->arg0 = arg0;
	wthread->arg1 = arg1;
	init_user_ctx(&wthread->uthread.u_ctx, (uintptr_t)&__wthread_run,
	              (uintptr_t)(wthread->stacktop));
	uth_attr.want_tls = FALSE;
	uthread_init((struct uthread*)wthread, &uth_attr);
	*thread = wthread;
	return 0;
}

void __wthread_generic_yield(struct wthread *wthread)
{
}

static void __wth_exit_cb(struct uthread *uthread, void *junk)
{
	struct wthread *wthread = (struct wthread*)uthread;
	struct wthread *temp_pth = 0;
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	__wthread_generic_yield(wthread);
	wthread->state = WTH_ZOMBIE;
	printd("uth %p exiting\n", uthread);
	TAILQ_INSERT_HEAD(&vcm->zombies, wthread, next);
	vcm->nr_zombies++;
}

/* In case we want to clean up */
static void wthread_destroy(struct wthread *wthread)
{
	struct uthread *uthread = (struct uthread*)wthread;
	uthread_cleanup(uthread);
	__wthread_free_stack(wthread);
	free(wthread);
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

struct wthread *wthread_self(void)
{
	return (struct wthread*)current_uthread;
}


/******************************************************************************/
/* Signal and trap stuff.  Tied in with the 2LS still, but consider extracting*/

/* Swap the contents of two user contexts (not just their pointers). */
static void swap_user_contexts(struct user_context *c1, struct user_context *c2)
{
	struct user_context temp_ctx;
	temp_ctx = *c1;
	*c1 = *c2;
	*c2 = temp_ctx;
}

/* Prep a wthread to run a signal handler.  The original context of the wthread
 * is saved, and a new context with a new stack is set up to run the signal
 * handler the next time the wthread is run. */
static void __wthread_prep_sighandler(struct wthread *wthread,
                                      void (*entry)(void),
                                      struct siginfo *info)
{
	struct user_context *ctx;

	wthread->sigdata = alloc_sigdata();
	if (info != NULL)
		wthread->sigdata->info = *info;
	init_user_ctx(&wthread->sigdata->u_ctx,
	              (uintptr_t)entry,
	              (uintptr_t)wthread->sigdata->stack);
	if (wthread->uthread.flags & UTHREAD_SAVED) {
		ctx = &wthread->uthread.u_ctx;
		if (wthread->uthread.flags & UTHREAD_FPSAVED) {
			wthread->sigdata->as = wthread->uthread.as;
			wthread->uthread.flags &= ~UTHREAD_FPSAVED;
		}
	} else {
		assert(current_uthread == &wthread->uthread);
		ctx = &vcpd_of(vcore_id())->uthread_ctx;
		save_fp_state(&wthread->sigdata->as);
	}
	swap_user_contexts(ctx, &wthread->sigdata->u_ctx);
}

/* Restore the context saved as the result of running a signal handler on a
 * wthread. This context will execute the next time the wthread is run. */
static void __wthread_restore_after_sighandler(struct wthread *wthread)
{
	wthread->uthread.u_ctx = wthread->sigdata->u_ctx;
	wthread->uthread.flags |= UTHREAD_SAVED;
	if (wthread->uthread.u_ctx.type == ROS_HW_CTX) {
		wthread->uthread.as = wthread->sigdata->as;
		wthread->uthread.flags |= UTHREAD_FPSAVED;
	}
	free_sigdata(wthread->sigdata);
	wthread->sigdata = NULL;
}

/* Callback when yielding a wthread after upon completion of a sighandler.  We
 * didn't save the current context on yeild, but that's ok because here we
 * restore the original saved context of the wthread and then treat this like a
 * normal voluntary yield. */
static void __exit_sighandler_cb(struct uthread *uthread, void *junk)
{
	__wthread_restore_after_sighandler((struct wthread*)uthread);
	__wth_yield_cb(uthread, 0);
}

/* Run a specific sighandler from the top of the sigdata stack. The 'info'
 * struct is prepopulated before the call is triggered as the result of a
 * reflected fault. */
static void __run_sighandler()
{
	struct wthread *me = wthread_self();
	__sigdelset(&me->sigpending, me->sigdata->info.si_signo);
	trigger_posix_signal(me->sigdata->info.si_signo,
	                     &me->sigdata->info,
	                     &me->sigdata->u_ctx);
	uthread_yield(FALSE, __exit_sighandler_cb, 0);
}

/* Run through all pending sighandlers and trigger them with a NULL info field.
 * These handlers are triggered as the result of a wthread_kill(), and thus
 * don't require individual 'info' structs. */
static void __run_pending_sighandlers()
{
	struct wthread *me = wthread_self();
	sigset_t andset = me->sigpending & (~me->sigmask);
	for (int i = 1; i < _NSIG; i++) {
		if (__sigismember(&andset, i)) {
			__sigdelset(&me->sigpending, i);
			trigger_posix_signal(i, NULL, &me->sigdata->u_ctx);
		}
	}
	uthread_yield(FALSE, __exit_sighandler_cb, 0);
}

/* If the given signal is unmasked, prep the wthread to run it's signal
 * handler, but don't run it yet. In either case, make the wthread runnable
 * again. Once the signal handler is complete, the original context will be
 * restored and restarted. */
static void __wthread_signal_and_restart(struct wthread *wthread,
                                          int signo, int code, void *addr)
{
	if (!__sigismember(&wthread->sigmask, signo)) {
		if (wthread->sigdata) {
			printf("Wthread sighandler faulted, signal: %d\n", signo);
			/* uthread.c already copied out the faulting ctx into the uth */
			print_user_context(&wthread->uthread.u_ctx);
			exit(-1);
		}
		struct siginfo info = {0};
		info.si_signo = signo;
		info.si_code = code;
		info.si_addr = addr;
		__wthread_prep_sighandler(wthread, __run_sighandler, &info);
	}
	wth_thread_runnable(&wthread->uthread);
}

/* If there are any pending signals, prep the wthread to run it's signal
 * handler. The next time the wthread is run, it will pop into it's signal
 * handler context instead of its original saved context. Once the signal
 * handler is complete, the original context will be restored and restarted. */
static void __wthread_prep_for_pending_posix_signals(wthread_t wthread)
{
	if (!wthread->sigdata && wthread->sigpending) {
		sigset_t andset = wthread->sigpending & (~wthread->sigmask);
		if (!__sigisemptyset(&andset)) {
			__wthread_prep_sighandler(wthread, __run_pending_sighandlers, NULL);
		}
	}
}

int wthread_kill(wthread_t thread, int signo)
{
	// Slightly racy with clearing of mask when triggering the signal, but
	// that's OK, as signals are inherently racy since they don't queue up.
	return sigaddset(&thread->sigpending, signo);
}

int wthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
	if (how != SIG_BLOCK && how != SIG_SETMASK && how != SIG_UNBLOCK) {
		errno = EINVAL;
		return -1;
	}

	wthread_t wthread = ((struct wthread*)current_uthread);
	if (oset)
		*oset = wthread->sigmask;
	switch (how) {
		case SIG_BLOCK:
			wthread->sigmask = wthread->sigmask | *set;
			break;
		case SIG_SETMASK:
			wthread->sigmask = *set;
			break;
		case SIG_UNBLOCK:
			wthread->sigmask = wthread->sigmask & ~(*set);
			break;
	}
	// Ensures any signals we just unmasked get processed if they are pending
	wthread_yield();
	return 0;
}

static void handle_div_by_zero(struct uthread *uthread, unsigned int err,
                               unsigned long aux)
{
	struct wthread *wthread = (struct wthread*)uthread;
	__wthread_signal_and_restart(wthread, SIGFPE, FPE_INTDIV, (void*)aux);
}

static void handle_gp_fault(struct uthread *uthread, unsigned int err,
                            unsigned long aux)
{
	struct wthread *wthread = (struct wthread*)uthread;
	__wthread_signal_and_restart(wthread, SIGSEGV, SEGV_ACCERR, (void*)aux);
}

static void handle_page_fault(struct uthread *uthread, unsigned int err,
                              unsigned long aux)
{
	struct vc_mgmt *vcm = &vc_mgmt[vcore_id()];
	struct wthread *wthread = (struct wthread*)uthread;
	if (!(err & PF_VMR_BACKED)) {
		__wthread_signal_and_restart(wthread, SIGSEGV, SEGV_MAPERR, (void*)aux);
	} else {
		/* stitching for the event handler.  sysc -> uth, uth -> sysc */
		uthread->local_sysc.u_data = uthread;
		uthread->sysc = &uthread->local_sysc;
		wthread->state = WTH_BLK_SYSC;
		/* one downside is that we'll never check the return val of the syscall.
		 * if we errored out, we wouldn't know til we PF'd again, and inspected
		 * the old retval/err and other sysc fields (make sure the PF is on the
		 * same addr, etc).  could run into this issue on truncated files too.
		 * */
		/* slightly 2LS specific XXX */
		syscall_async(&uthread->local_sysc, SYS_populate_va, aux, 1);
		vcm->nr_blk_sysc++;
		if (!register_evq(&uthread->local_sysc, vcm->ev_q)) {
			/* Lost the race with the call being done.  The kernel won't send the
			 * event.  Just restart him. */
			restart_thread(&uthread->local_sysc);
		}
	}
}

void wth_thread_refl_fault(struct uthread *uthread, unsigned int trap_nr,
                           unsigned int err, unsigned long aux)
{
	struct wthread *wthread = (struct wthread*)uthread;
	/* XXX 2LS specific (active list too) */
	wthread->state = WTH_BLK_SYSC;

	/* TODO: RISCV/x86 issue! (0 is divby0, 14 is PF, etc) */
#if defined(__i386__) || defined(__x86_64__) 
	switch(trap_nr) {
		case 0:
			handle_div_by_zero(uthread, err, aux);
			break;
		case 13:
			handle_gp_fault(uthread, err, aux);
			break;
		case 14:
			handle_page_fault(uthread, err, aux);
			break;
		default:
			printf("Wthread has unhandled fault: %d\n", trap_nr);
			/* Note that uthread.c already copied out our ctx into the uth struct */
			print_user_context(&uthread->u_ctx);
			exit(-1);
	}
#else
	#error "Handling hardware faults is currently only supported on x86"
#endif
}
