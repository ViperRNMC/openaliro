// Memory allocation and timing facade: qmalloc, qcalloc, qfree wrap Zephyr k_* heap routines; qrtc_get_us returns monotonic microseconds since boot.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 * woz_alloc.h - thin Zephyr heap + monotonic-clock wrappers.
 */
#ifndef WOZ_ALLOC_H
#define WOZ_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

// Allocate size bytes; wrapper around k_malloc.
static inline void *qmalloc(size_t size)
{
	return k_malloc(size);
}

// Allocate and zero-initialize nb_items elements of item_size bytes each; wrapper around k_calloc.
static inline void *qcalloc(size_t nb_items, size_t item_size)
{
	return k_calloc(nb_items, item_size);
}

// Deallocate memory previously allocated by qmalloc or qcalloc; wrapper around k_free.
static inline void qfree(void *ptr)
{
	k_free(ptr);
}

/** Monotonic microseconds since boot. */
static inline int64_t qrtc_get_us(void)
{
	return (int64_t)k_ticks_to_us_floor64(k_uptime_ticks());
}

#endif /* WOZ_ALLOC_H */
