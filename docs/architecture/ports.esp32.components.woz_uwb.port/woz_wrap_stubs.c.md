<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/components/woz_uwb/port/woz_wrap_stubs.c`

Minimal ESP-IDF port of the essential RX-callback shim.
The Nordic build routes DW3000 RX events through uwb_rxdiag.c's
__wrap_dwt_setcallbacks -> shim_rxok, which (after the blob's own
prepoll_rx_rearm arms the SP3 POLL window) calls ccc_shim_rx_try_prepoll to
decrypt+warm the NEXT block's STS.  That bootstrap warm is what flips
g_warm_valid true so the POLL window ever gets armed and Response_0 sent.
This port omits uwb_rxdiag.c wholesale (its heartbeat needs Zephyr k_work,
which the compat layer does not provide), so without this shim dwt_setcallbacks
installs prepoll_rx_rearm directly, ccc_shim_rx_try_prepoll is never reached,
g_warm_valid stays false, and the responder receives Pre-POLLs but never
replies.  Re-create only the essential chain here (no k_work, no diagnostics).
Also keeps the dwt_configurestsmode pass-through the essential RX path needs.

**discussed in** [`docs/porting.md`](../../porting.md), [`ports/esp32/apps/reader/README.md`](../../../ports/esp32/apps/reader/README.md), [`ports/esp32/components/woz_uwb/README.md`](../../../ports/esp32/components/woz_uwb/README.md)

## API

### `void __wrap_dwt_configurestsmode(uint8_t stsMode)`
`ports/esp32/components/woz_uwb/port/woz_wrap_stubs.c:25`

Wrapped __real_dwt_configurestsmode with no added behavior; forwards stsMode unchanged.

### `static void shim_rxok(const dwt_cb_data_t *d)`
`ports/esp32/components/woz_uwb/port/woz_wrap_stubs.c:45`

RX-good shim: feed the empirical STS-index tracker, run the blob's arm
(prepoll_rx_rearm), then — unless this RX is the awaited POLL — decode the
Pre-POLL to warm the next block's STS.  Mirrors uwb_rxdiag.c:shim_rxok minus
the tallies/cadence/event logging.

### `static void shim_rxto(const dwt_cb_data_t *d)`
`ports/esp32/components/woz_uwb/port/woz_wrap_stubs.c:61`

RX-timeout callback shim; forwards the event to g_blob_rxto if a handler is registered, otherwise no-op.

### `static void shim_rxerr(const dwt_cb_data_t *d)`
`ports/esp32/components/woz_uwb/port/woz_wrap_stubs.c:69`

RX-error callback shim; forwards the event to g_blob_rxerr if a handler is registered, otherwise no-op.

### `static void shim_txdone(const dwt_cb_data_t *d)`
`ports/esp32/components/woz_uwb/port/woz_wrap_stubs.c:77`

TX-done callback shim; forwards the event to g_blob_txdone if a handler is registered, otherwise no-op.

### `void __wrap_dwt_setcallbacks(dwt_callbacks_s *callbacks)`
`ports/esp32/components/woz_uwb/port/woz_wrap_stubs.c:85`

Intercept the callback registration and insert the RX-good bootstrap shim.
