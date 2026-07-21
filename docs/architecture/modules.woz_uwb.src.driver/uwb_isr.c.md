<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/driver/uwb_isr.c`

@file uwb_isr.c — DW3000 interrupt-callback registration (implementation).

**depends on** [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_port/include/woz_port.h`](../modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/driver/uwb_isr.h`](uwb_isr.h.md), [`modules/woz_uwb/src/facade/trace.h`](../modules.woz_uwb.src.facade/trace.h.md)

## API

### `static void cb_rx_ok(const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_isr.c:19`

@brief RX-good callback: peek the frame header, log via WOZ_TRACE, then re-arm RX.

### `static void cb_rx_to(const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_isr.c:38`

@brief RX-timeout callback: frame-wait window expired; re-arms RX.

### `static void cb_rx_err(const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_isr.c:45`

@brief RX-error callback: frame heard but rejected; logs status and re-arms RX.

### `static void cb_tx_done(const dwt_cb_data_t *d)`
`modules/woz_uwb/src/driver/uwb_isr.c:52`

@brief TX-done callback: our transmitted frame left the antenna.

### `int uwb_isr_register(void)`
`modules/woz_uwb/src/driver/uwb_isr.c:57`

@brief Install RX/TX callbacks and unmask the SYS_ENABLE bits; returns 0.
