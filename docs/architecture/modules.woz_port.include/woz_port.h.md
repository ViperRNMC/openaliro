<!-- generated documentation — edit the source, not this file -->
# `modules/woz_port/include/woz_port.h`

*No module docstring. First commit: "modules: promote the platform contract to modules/woz_port".*

**used by** [`modules/woz_aliro/src/aliro_lat.c`](../modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](../modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/driver/uwb_isr.c`](../modules.woz_uwb.src.driver/uwb_isr.c.md), [`modules/woz_uwb/src/driver/uwb_min.c`](../modules.woz_uwb.src.driver/uwb_min.c.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](../modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/fira/fira_session.c`](../modules.woz_uwb.src.fira/fira_session.c.md)  ·  **discussed in** [`README.md`](../../../README.md), [`docs/porting.md`](../../porting.md), [`modules/README.md`](../../../modules/README.md), [`modules/woz_port/README.md`](../../../modules/woz_port/README.md), [`ports/README.md`](../../../ports/README.md)

## API

### `static inline void *woz_malloc(size_t size)`
`modules/woz_port/include/woz_port.h:162`

@brief Allocate size bytes.
@param size Number of bytes to allocate.
@return Pointer to allocated memory, or NULL on failure.

### `static inline void *woz_calloc(size_t n, size_t size)`
`modules/woz_port/include/woz_port.h:172`

@brief Allocate and zero-initialize n elements of size bytes each.
@param n Number of elements.
@param size Bytes per element.
@return Pointer to allocated and zeroed memory, or NULL on failure.

### `static inline void woz_free(void *ptr)`
`modules/woz_port/include/woz_port.h:180`

@brief Deallocate memory.
@param ptr Pointer to memory to free (may be NULL).

### `static inline int64_t woz_uptime_us(void)`
`modules/woz_port/include/woz_port.h:188`

@brief Monotonic microseconds since boot.
@return Microseconds elapsed since system start.

**called by** `woz_cycle_get_32`, `woz_uptime_ms`

### `static inline int64_t woz_uptime_ms(void)`
`modules/woz_port/include/woz_port.h:199`

@brief Monotonic milliseconds since boot.
@return Milliseconds elapsed since system start.

**calls** `woz_uptime_us`

### `static inline void woz_sleep_ms(int32_t ms)`
`modules/woz_port/include/woz_port.h:207`

@brief Sleep for a given number of milliseconds (host-test stub).
@param ms milliseconds to sleep; ignored in deterministic host tests.

### `static inline void woz_sleep_us(int64_t us)`
`modules/woz_port/include/woz_port.h:215`

@brief Sleep for a given number of microseconds (host-test stub).
@param us microseconds to sleep; ignored in deterministic host tests.

### `static inline uint32_t woz_cycle_get_32(void)`
`modules/woz_port/include/woz_port.h:223`

@brief Retrieve a 32-bit cycle counter with microsecond resolution.
@return current uptime in microseconds, cast to uint32_t.

**calls** `woz_uptime_us`

### `typedef int woz_mutex_t`
`modules/woz_port/include/woz_port.h:230`

@brief Opaque mutex type for host tests (single-threaded, no-op).

### `static inline void woz_mutex_init(woz_mutex_t *m)`
`modules/woz_port/include/woz_port.h:235`

@brief Initialize a mutex (host-test stub).
@param m pointer to mutex to initialize; no-op in single-threaded tests.

### `static inline void woz_mutex_lock(woz_mutex_t *m)`
`modules/woz_port/include/woz_port.h:243`

@brief Acquire a mutex (host-test stub).
@param m pointer to mutex to lock; no-op in single-threaded tests.

### `static inline void woz_mutex_unlock(woz_mutex_t *m)`
`modules/woz_port/include/woz_port.h:251`

@brief Release a mutex (host-test stub).
@param m pointer to mutex to unlock; no-op in single-threaded tests.
