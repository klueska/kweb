#ifndef TSC_H
#define TSC_H

#if defined(__i386__) || defined(__x86_64__)
#else
#error "Platform not supported for read_tsc()"
#endif

#include <sys/time.h>
#include <stdint.h>

#ifdef __i386__
static inline uint64_t read_tsc(void)
{
  uint64_t tsc;
  asm volatile("lfence;rdtsc" : "=A" (tsc));
  return tsc;
}
#elif __x86_64__
static inline uint64_t read_tsc(void)
{
  uint32_t lo, hi;
  /* We cannot use "=A", since this would use %rax on x86_64 */
  asm volatile("lfence;rdtsc" : "=a" (lo), "=d" (hi));
  return (uint64_t)hi << 32 | lo;
}
#endif

static inline uint64_t get_tsc_freq()
{
  struct timeval prev;
  struct timeval curr;
  uint64_t beg = read_tsc();
  gettimeofday(&prev, NULL);
  while(1) {
    gettimeofday(&curr, NULL);
    if(curr.tv_sec > (prev.tv_sec+1) ||
       (curr.tv_sec > prev.tv_sec && curr.tv_usec > prev.tv_usec))
      break;
  }
  uint64_t end = read_tsc();
  return end-beg;
}

#endif // TSC_H
