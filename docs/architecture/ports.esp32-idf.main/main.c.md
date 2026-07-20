<!-- generated documentation — edit the source, not this file -->
# `ports/esp32-idf/main/main.c`

Woz UWB ranging engine on ESP32-S3 (ESP-IDF) — minimal bring-up app.
Binds a canned URSK and starts the CCC DS-TWR responder on the DW3000, then
polls for a range. With no iPhone/initiator present this proves the SPI +
DW3000 + CCC init path comes up on ESP32-S3; a live range needs a peer that
drives the DS-TWR exchange (an Aliro Wallet, or a second board as initiator).
The demo responder lifecycle + interactive console live in app_shell.c.
Ported from ports/esp32s3/sample/src/main.c (the Zephyr scaffold).

**depends on** [`ports/esp32-idf/main/app_shell.h`](app_shell.h.md)

## API

### `void app_main(void)`
`ports/esp32-idf/main/main.c:34`

Application entry point: brings up the DW3000 responder, the Aliro BLE reader,
and the interactive shell, then polls for ranging results.
Silences the CCC shim's per-frame STS trace (WARN level only) because logging
on the delayed-TX reply path can blow the reply window; other subsystems keep
their normal log level. app_responder_start() performs the full DW3000
bring-up chain (woz_uwb_start_aliro -> ccc_prepoll_listen ->
uwb_min_radio_init). aliro_reader_start() brings up the BLE transport and
session/transaction layer independently of the demo responder; the
URSK-driven UWB start happens inside the reader once the Phase-3 handshake is
implemented. Never returns: loops forever logging the last UWB range every
500 ms when available.
