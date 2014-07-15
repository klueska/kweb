/* Basic TSC compatability helpers, callable from Akaros or Linux. Supports:
 * 		uint64_t read_tsc()
 * 		uint64_t read_tsc_serialized()
 * 		uint64_t get_tsc_freq()
 * 		uint64_t get_tsc_overhead()
 *
 * Note this relies on specifics of procinfo, which isn't stable.  If procinfo
 * changes, this will need to change as well.  You'll know when this doesn't
 * compile (say, if timing_overhead moves).  */

#ifndef _TSC_COMPAT_H
#define _TSC_COMPAT_H

#if defined(__i386__) || defined(__x86_64__)
#else
#error "Platform not supported for read_tsc()"
#endif

#ifdef __cplusplus
	extern "C" {
#endif

#ifdef __ros__

#include <arch/arch.h>
#include <ros/procinfo.h>

static inline uint64_t get_tsc_freq(void)
{
	return __procinfo.tsc_freq;
}

static inline uint64_t get_tsc_overhead(void)
{
	return __procinfo.timing_overhead;
}

#else /* ! _ros_ (linux) */

#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>

/* Akaros has this helper in ros/common.h. (it returns a bool btw)
 *
 * We wraparound if UINT_MAX < a * b, which is also UINT_MAX / a < b. */
static inline int mult_will_overflow_u64(uint64_t a, uint64_t b)
{
	if (!a)
		return false;
	return (uint64_t)(-1) / a < b;
}

# ifdef __i386__

static inline uint64_t read_tsc(void)
{
	uint64_t tsc;
	asm volatile("rdtsc" : "=A" (tsc));
	return tsc;
}

static inline uint64_t read_tsc_serialized(void)
{
	uint64_t tsc;
	asm volatile("lfence; rdtsc" : "=A" (tsc));
	return tsc;
}

# elif __x86_64__

static inline uint64_t read_tsc(void)
{
	uint32_t lo, hi;
	/* We cannot use "=A", since this would use %rax on x86_64 */
	asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
	return (uint64_t)hi << 32 | lo;
}

static inline uint64_t read_tsc_serialized(void)
{
	uint32_t lo, hi;
	asm volatile("lfence; rdtsc" : "=a" (lo), "=d" (hi));
	return (uint64_t)hi << 32 | lo;
}

# else
#  error "Which arch is this?"
# endif /* __i386__ | __x86_64__ */

static inline uint64_t get_tsc_freq(void)
{
	struct timeval prev;
	struct timeval curr;
	uint64_t beg = read_tsc_serialized();
	gettimeofday(&prev, 0);
	while (1) {
		gettimeofday(&curr, 0);
		if (curr.tv_sec > (prev.tv_sec + 1) ||
			(curr.tv_sec > prev.tv_sec && curr.tv_usec > prev.tv_usec))
			break;
	}
	uint64_t end = read_tsc_serialized();
	return end - beg;
}

/* Don't have a good way to get the overhead on Linux in userspace. */
static inline uint64_t get_tsc_overhead(void)
{
	return 0;
}

#endif /* ! _ros_ */

#ifdef __cplusplus
	}
#endif

#endif /* _TSC_COMPAT_H */
