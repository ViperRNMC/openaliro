/* Host shim for <zephyr/kernel.h> — heap + monotonic clock backed by libc.
 * Only the surface the pure-logic modules touch (woz_alloc.h + fira uptime). */
#ifndef WOZ_HOST_SHIM_KERNEL_H
#define WOZ_HOST_SHIM_KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static inline void *k_malloc(size_t size)
{
	return malloc(size);
}

static inline void *k_calloc(size_t n, size_t size)
{
	return calloc(n, size);
}

static inline void k_free(void *ptr)
{
	free(ptr);
}

/** Monotonic microseconds since an arbitrary epoch. */
static inline int64_t woz_host_monotonic_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* woz_alloc.h models a tick as one microsecond, so keep ticks == us here. */
static inline int64_t k_uptime_ticks(void)
{
	return woz_host_monotonic_us();
}

static inline int64_t k_ticks_to_us_floor64(int64_t ticks)
{
	return ticks;
}

static inline int64_t k_uptime_get(void)
{
	return woz_host_monotonic_us() / 1000; /* milliseconds */
}

static inline uint32_t k_uptime_get_32(void)
{
	return (uint32_t)k_uptime_get();
}

/** Cycle counter for the listener's latency probes; us resolution is plenty. */
static inline uint32_t k_cycle_get_32(void)
{
	return (uint32_t)woz_host_monotonic_us();
}

#endif /* WOZ_HOST_SHIM_KERNEL_H */
