/* ESP-IDF compat for <zephyr/kernel.h> — the k_* surface the woz_uwb engine +
 * dw3000 platform glue touch, backed by FreeRTOS + esp_timer. On-silicon twin
 * of tests/host/shim/zephyr/kernel.h (which is libc-backed for host tests).
 * Additive: the shared engine .c files compile unmodified because this resolves
 * first on the include path. See ports/esp32-idf/README.md. */
#ifndef WOZ_ESP_COMPAT_KERNEL_H
#define WOZ_ESP_COMPAT_KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_rom_sys.h" /* esp_rom_delay_us */
#include "esp_cpu.h"     /* esp_cpu_get_cycle_count */

/* --- heap (woz_alloc.h) --- */
static inline void *k_malloc(size_t size) { return malloc(size); }
// Allocate and zero-initialize an array of n elements of size bytes, Zephyr k_calloc compatibility shim.
// Thin wrapper over libc calloc.
static inline void *k_calloc(size_t n, size_t size) { return calloc(n, size); }
// Free a block previously allocated with k_calloc, Zephyr k_free compatibility shim.
// Thin wrapper over libc free.
static inline void k_free(void *ptr) { free(ptr); }

/* --- monotonic clock ---
 * woz_alloc.h models a tick as one microsecond, so keep ticks == us. */
static inline int64_t k_uptime_ticks(void) { return esp_timer_get_time(); }
// Convert a tick count to microseconds, floored, Zephyr k_ticks_to_us_floor64 compatibility shim.
// Identity conversion: this port's tick unit is already microseconds.
static inline int64_t k_ticks_to_us_floor64(int64_t ticks) { return ticks; }
// Return the current uptime in milliseconds, Zephyr k_uptime_get compatibility shim.
// Derived from esp_timer_get_time() (microseconds) divided down to milliseconds.
static inline int64_t k_uptime_get(void) { return esp_timer_get_time() / 1000; }
// Return the current uptime in milliseconds as a 32-bit value, truncating the 64-bit k_uptime_get result.
// Zephyr k_uptime_get_32 compatibility shim.
static inline uint32_t k_uptime_get_32(void) { return (uint32_t)k_uptime_get(); }

/* CPU cycle counter (ccc_shim_rx.c per-arm timestamp). */
static inline uint32_t k_cycle_get_32(void) { return esp_cpu_get_cycle_count(); }

/* --- sleeps / busy-waits --- */
static inline void k_busy_wait(uint32_t usec) { esp_rom_delay_us(usec); }
// Busy-wait for usec microseconds, Zephyr k_usleep compatibility shim.
// Truncates usec to 32 bits before delaying; not suitable for very large delays.
static inline void k_usleep(int64_t usec) { esp_rom_delay_us((uint32_t)usec); }
// Sleep the calling task for ms milliseconds, Zephyr k_msleep compatibility shim.
// Returns immediately without sleeping if ms is zero or negative.
static inline void k_msleep(int32_t ms)
{
	if (ms <= 0) {
		return;
	}
	vTaskDelay(pdMS_TO_TICKS(ms));
}

/* --- mutex (woz_aliro reader trust-store guard) ---
 * Only the blocking K_FOREVER lock is modelled; the Aliro reader never takes a
 * timed lock. k_mutex_init is called once before any lock, matching Zephyr. */
#define K_FOREVER portMAX_DELAY

struct k_mutex {
	SemaphoreHandle_t sem;
};

static inline int k_mutex_init(struct k_mutex *m)
{
	m->sem = xSemaphoreCreateMutex();
	return m->sem ? 0 : -1;
}

// Take the mutex, blocking. Only K_FOREVER is supported; timeout is passed through.
static inline int k_mutex_lock(struct k_mutex *m, TickType_t timeout)
{
	return xSemaphoreTake(m->sem, timeout) == pdTRUE ? 0 : -1;
}

static inline int k_mutex_unlock(struct k_mutex *m)
{
	return xSemaphoreGive(m->sem) == pdTRUE ? 0 : -1;
}

#endif /* WOZ_ESP_COMPAT_KERNEL_H */
