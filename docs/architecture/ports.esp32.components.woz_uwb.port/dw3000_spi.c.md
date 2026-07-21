<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/components/woz_uwb/port/dw3000_spi.c`

ESP-IDF SPI backend for the DW3000 decadriver — implements dw3000_spi.h.
Replaces the Zephyr deps/dw3000/platform/dw3000_spi.c (not compiled here).
CS is a plain GPIO (spics_io_num = -1), matching the Zephyr cs-gpios model, so
the wakeup path can hold CS low ~500us. Each DW3000 command is one CS-low
full-duplex transfer: header + body assembled in a DMA-capable, word-aligned
bounce buffer; on reads the body slice of the RX buffer is copied back.

**depends on** [`ports/esp32/components/woz_uwb/port/board_pins.h`](board_pins.h.md)  ·  **discussed in** [`docs/porting.md`](../../porting.md), [`ports/esp32/apps/reader/README.md`](../../../ports/esp32/apps/reader/README.md), [`ports/esp32/components/woz_uwb/README.md`](../../../ports/esp32/components/woz_uwb/README.md)

## API

### `int dw3000_spi_init(void)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:42`

Bring up the DW3000 SPI bus and CS GPIO for this port.
Idempotent: returns 0 immediately if already initialized. Configures CS as an
active-low output (idle high), initializes the SPI bus with DMA disabled
(transfers <=64 B use CPU data registers directly; larger ones are chunked by
dw_xfer), and adds both a slow-clock and fast-clock device handle on the bus.
Starts the active device at slow speed. Only the DW3000 sits on this bus.
Returns 0 on success, -1 if bus init or device add fails.

### `void dw3000_spi_speed_slow(void)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:100`

Switch subsequent DW3000 SPI transfers to the slow-clock device handle.

### `void dw3000_spi_speed_fast(void)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:102`

Switch subsequent DW3000 SPI transfers to the fast-clock device handle.

### `void dw3000_spi_fini(void)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:106`

Tears down the DW3000 SPI bus: removes the slow and fast SPI device handles if present, then frees the SPI bus on WOZ_DW3000_SPI_HOST.
Safe to call when devices were never added (each handle is checked for non-NULL before removal).

### `static int32_t dw_xfer(const uint8_t *hdr, uint16_t hlen, const uint8_t *body, uint16_t blen, uint8_t *rx_body, const uint8_t *crc)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:121`

One CS-low command: [header][body|zeros][crc?]; capture RX body slice when
rx_body != NULL.

**called by** `dw3000_spi_read`, `dw3000_spi_write`, `dw3000_spi_write_crc`

### `int32_t dw3000_spi_read(uint16_t headerLength, uint8_t *headerBuffer, uint16_t readLength, uint8_t *readBuffer)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:173`

Read from the DW3000 over SPI: sends a header then clocks in readLength bytes.
Thin wrapper over dw_xfer with no body write and no CRC. Returns whatever
dw_xfer returns.

**calls** `dw_xfer`

### `int32_t dw3000_spi_write(uint16_t headerLength, const uint8_t *headerBuffer, uint16_t bodyLength, const uint8_t *bodyBuffer)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:183`

Write to the DW3000 over SPI: sends a header followed by a body, no CRC byte.
Thin wrapper over dw_xfer with no read capture. Returns whatever dw_xfer
returns.

**calls** `dw_xfer`

### `int32_t dw3000_spi_write_crc(uint16_t headerLength, const uint8_t *headerBuffer, uint16_t bodyLength, const uint8_t *bodyBuffer, uint8_t crc8)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:193`

Write to the DW3000 over SPI with a trailing CRC8 byte appended after the body.
Thin wrapper over dw_xfer with no read capture. Returns whatever dw_xfer
returns.

**calls** `dw_xfer`

### `void dw3000_spi_wakeup(void)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:204`

Wake the DW3000 from sleep by toggling CS.
Drives CS low for ~500us then releases it high, per the Qorvo CS-toggle wake
sequence. Blocks for the duration of the delay.

### `void dw3000_spi_trace_output(void)`
`ports/esp32/components/woz_uwb/port/dw3000_spi.c:213`

No-op: SPI transaction tracing is not implemented in this port.
