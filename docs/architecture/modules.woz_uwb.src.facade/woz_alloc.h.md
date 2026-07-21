<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/woz_alloc.h`

Memory allocation and timing facade: qmalloc, qcalloc, qfree wrap the platform heap;
qrtc_get_us returns monotonic microseconds since boot.

**depends on** [`modules/woz_port/include/woz_port.h`](../modules.woz_port.include/woz_port.h.md)  ·  **used by** [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](../modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](../modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.c`](../modules.woz_uwb.src.aliro/aliro_uwb_msg_builder.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](../modules.woz_uwb.src.aliro/aliro_uwb_session.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](../modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.c`](../modules.woz_uwb.src.driver/uwb_rxdiag.c.md)  ·  **discussed in** [`docs/porting-esp32.md`](../../porting-esp32.md)

## API

### `static inline void *qmalloc(size_t size)`
`modules/woz_uwb/src/facade/woz_alloc.h:21`

@brief Allocate size bytes.
@param size Number of bytes to allocate.
@return Pointer to allocated memory, or NULL on failure.

### `static inline void *qcalloc(size_t nb_items, size_t item_size)`
`modules/woz_uwb/src/facade/woz_alloc.h:32`

@brief Allocate and zero-initialize nb_items elements of item_size bytes each.
@param nb_items Number of items.
@param item_size Bytes per item.
@return Pointer to allocated and zeroed memory, or NULL on failure.

### `static inline void qfree(void *ptr)`
`modules/woz_uwb/src/facade/woz_alloc.h:41`

@brief Deallocate memory previously allocated by qmalloc or qcalloc.
@param ptr Pointer to memory to free (may be NULL).

### `static inline int64_t qrtc_get_us(void)`
`modules/woz_uwb/src/facade/woz_alloc.h:50`

@brief Monotonic microseconds since boot.
@return Microseconds elapsed since system start.
