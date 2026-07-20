<!-- generated documentation — edit the source, not this file -->
# `ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h`

ESP-IDF compat for <zephyr/kernel.h> — the k_* surface the woz_uwb engine +
dw3000 platform glue touch, backed by FreeRTOS + esp_timer. On-silicon twin
of tests/host/shim/zephyr/kernel.h (which is libc-backed for host tests).
Additive: the shared engine .c files compile unmodified because this resolves
first on the include path. See ports/esp32-idf/README.md.

**used by** [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](../modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_isr.c`](../modules.woz_uwb.src.driver/uwb_isr.c.md), [`modules/woz_uwb/src/driver/uwb_min.c`](../modules.woz_uwb.src.driver/uwb_min.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](../modules.woz_uwb.src.driver/uwb_rxdiag.c.md), [`modules/woz_uwb/src/driver/uwb_selftest.c`](../modules.woz_uwb.src.driver/uwb_selftest.c.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](../modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/facade/woz_logfmt.c`](../modules.woz_uwb.src.facade/woz_logfmt.c.md), [`modules/woz_uwb/src/fira/fira_session.c`](../modules.woz_uwb.src.fira/fira_session.c.md), [`modules/woz_uwb/src/shell/aliro_shell.c`](../modules.woz_uwb.src.shell/aliro_shell.c.md), [`ports/esp32s3/sample/src/main.c`](../ports.esp32s3.sample.src/main.c.md)  ·  **discussed in** [`ports/esp32-idf/README.md`](../../../ports/esp32-idf/README.md), [`ports/esp32-idf/components/woz_uwb/README.md`](../../../ports/esp32-idf/components/woz_uwb/README.md)

## API

### `static inline void *k_malloc(size_t size)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:21`

--- heap (woz_alloc.h) ---

### `static inline void *k_calloc(size_t n, size_t size)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:24`

Allocate and zero-initialize an array of n elements of size bytes, Zephyr k_calloc compatibility shim.
Thin wrapper over libc calloc.

### `static inline void k_free(void *ptr)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:27`

Free a block previously allocated with k_calloc, Zephyr k_free compatibility shim.
Thin wrapper over libc free.

### `static inline int64_t k_uptime_ticks(void)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:31`

--- monotonic clock ---
woz_alloc.h models a tick as one microsecond, so keep ticks == us.

### `static inline int64_t k_ticks_to_us_floor64(int64_t ticks)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:34`

Convert a tick count to microseconds, floored, Zephyr k_ticks_to_us_floor64 compatibility shim.
Identity conversion: this port's tick unit is already microseconds.

### `static inline int64_t k_uptime_get(void)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:37`

Return the current uptime in milliseconds, Zephyr k_uptime_get compatibility shim.
Derived from esp_timer_get_time() (microseconds) divided down to milliseconds.

**called by** `k_uptime_get_32`

### `static inline uint32_t k_uptime_get_32(void)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:40`

Return the current uptime in milliseconds as a 32-bit value, truncating the 64-bit k_uptime_get result.
Zephyr k_uptime_get_32 compatibility shim.

**calls** `k_uptime_get`

### `static inline uint32_t k_cycle_get_32(void)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:43`

CPU cycle counter (ccc_shim_rx.c per-arm timestamp).

### `static inline void k_busy_wait(uint32_t usec)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:46`

--- sleeps / busy-waits ---

### `static inline void k_usleep(int64_t usec)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:49`

Busy-wait for usec microseconds, Zephyr k_usleep compatibility shim.
Truncates usec to 32 bits before delaying; not suitable for very large delays.

### `static inline void k_msleep(int32_t ms)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:52`

Sleep the calling task for ms milliseconds, Zephyr k_msleep compatibility shim.
Returns immediately without sleeping if ms is zero or negative.

### `static inline int k_mutex_lock(struct k_mutex *m, TickType_t timeout)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:76`

Take the mutex, blocking. Only K_FOREVER is supported; timeout is passed through.

### `static inline int k_mutex_lock(struct k_mutex *m, TickType_t timeout)`
`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h:81`

Take the mutex, blocking. Only K_FOREVER is supported; timeout is passed through.

<details><summary>Undocumented (2)</summary>

- `k_mutex_init`
- `k_mutex_unlock`

</details>
