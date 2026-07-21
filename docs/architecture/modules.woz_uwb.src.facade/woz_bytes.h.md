<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/woz_bytes.h`

*No module docstring. First commit: "port: replace the Zephyr compat shims with a neutral woz_port.h contract".*

**used by** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](../modules.woz_uwb.src.ccc/ccc_shim_rx.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_wrap.c`](../modules.woz_uwb.src.ccc/ccc_shim_wrap.c.md), [`modules/woz_uwb/src/ccc/ccc_sts.c`](../modules.woz_uwb.src.ccc/ccc_sts.c.md)  ·  **discussed in** [`docs/porting.md`](../../porting.md), [`ports/esp32/components/woz_uwb/README.md`](../../../ports/esp32/components/woz_uwb/README.md)

## API

### `static inline uint16_t sys_get_le16(const uint8_t *p)`
`modules/woz_uwb/src/facade/woz_bytes.h:32`

@brief Read a 16-bit little-endian value from a byte buffer.
@param p Pointer to the byte buffer.
@return The 16-bit value in native byte order.

### `static inline uint32_t sys_get_le32(const uint8_t *p)`
`modules/woz_uwb/src/facade/woz_bytes.h:42`

@brief Read a 32-bit little-endian value from a byte buffer.
@param p Pointer to the byte buffer.
@return The 32-bit value in native byte order.

### `static inline uint16_t sys_get_be16(const uint8_t *p)`
`modules/woz_uwb/src/facade/woz_bytes.h:53`

@brief Read a 16-bit big-endian value from a byte buffer.
@param p Pointer to the byte buffer.
@return The 16-bit value in native byte order.

### `static inline uint32_t sys_get_be32(const uint8_t *p)`
`modules/woz_uwb/src/facade/woz_bytes.h:63`

@brief Read a 32-bit big-endian value from a byte buffer.
@param p Pointer to the byte buffer.
@return The 32-bit value in native byte order.

### `static inline void sys_put_le16(uint16_t v, uint8_t *p)`
`modules/woz_uwb/src/facade/woz_bytes.h:74`

@brief Write a 16-bit value to a byte buffer in little-endian order.
@param v The value to write.
@param p Pointer to the byte buffer.

### `static inline void sys_put_le32(uint32_t v, uint8_t *p)`
`modules/woz_uwb/src/facade/woz_bytes.h:85`

@brief Write a 32-bit value to a byte buffer in little-endian order.
@param v The value to write.
@param p Pointer to the byte buffer.

### `static inline void sys_put_be16(uint16_t v, uint8_t *p)`
`modules/woz_uwb/src/facade/woz_bytes.h:98`

@brief Write a 16-bit value to a byte buffer in big-endian order.
@param v The value to write.
@param p Pointer to the byte buffer.

### `static inline void sys_put_be32(uint32_t v, uint8_t *p)`
`modules/woz_uwb/src/facade/woz_bytes.h:109`

@brief Write a 32-bit value to a byte buffer in big-endian order.
@param v The value to write.
@param p Pointer to the byte buffer.
