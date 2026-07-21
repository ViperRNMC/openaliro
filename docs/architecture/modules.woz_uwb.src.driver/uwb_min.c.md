<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/driver/uwb_min.c`

@file uwb_min.c — DW3110 bring-up driver (implementation).

**depends on** [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](../modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](uwb_min.h.md)  ·  **discussed in** [`docs/porting-esp32.md`](../../porting-esp32.md), [`docs/porting.md`](../../porting.md)

## API

### `static int uwb_probe_ensure(void)`
`modules/woz_uwb/src/driver/uwb_min.c:23`

@brief Bring the SDK up to "probed" state on first call; no-op afterwards.

**called by** `uwb_min_read_chipid`, `uwb_radio_ensure_init`

### `static int uwb_radio_ensure_init(void)`
`modules/woz_uwb/src/driver/uwb_min.c:101`

@brief Bring the SDK up to "radio configured + LEDs on" state.

**called by** `uwb_min_radio_init`, `uwb_min_selftest`, `uwb_min_twr_prep`  ·  **calls** `uwb_probe_ensure`

### `int uwb_min_radio_init(void)`
`modules/woz_uwb/src/driver/uwb_min.c:139`

@brief Ensure the DW3110 is fully initialised (probe + initialise + configure + LEDs).

**calls** `uwb_radio_ensure_init`

### `int uwb_min_hw_reset(void)`
`modules/woz_uwb/src/driver/uwb_min.c:144`

@brief Pulse the DW3110 RST line low to force a hardware reset.

### `int uwb_min_read_chipid(uint32_t *id_out)`
`modules/woz_uwb/src/driver/uwb_min.c:156`

@brief Read the DW3000-family DEV_ID register over SPI.

**calls** `uwb_probe_ensure`

### `int uwb_min_selftest(struct uwb_selftest_result *out)`
`modules/woz_uwb/src/driver/uwb_min.c:181`

@brief Radio self-test: configure, TX one frame, then arm RX.

**calls** `uwb_radio_ensure_init`

### `int uwb_min_twr_prep(void)`
`modules/woz_uwb/src/driver/uwb_min.c:275`

@brief Configure the radio for the raw SS-TWR loopback (SP3-ND, ch9, code11); no STS.

**called by** `uwb_min_twr_poll`  ·  **calls** `uwb_radio_ensure_init`

### `void uwb_min_twr_exchange(struct uwb_twr_frame *f)`
`modules/woz_uwb/src/driver/uwb_min.c:296`

@brief Run one POLL/RESP exchange; the STS must already be programmed.

**called by** `uwb_min_twr_poll`

### `int uwb_min_twr_poll(uint32_t n, uint32_t period_ms, struct uwb_twr_result *out)`
`modules/woz_uwb/src/driver/uwb_min.c:349`

@brief Raw static-STS SS-TWR initiator burst (bench probe).

**calls** `uwb_min_twr_exchange`, `uwb_min_twr_prep`
