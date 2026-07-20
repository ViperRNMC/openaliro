<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_ranging.c`

UWB ranging bring-up and lifecycle for the Aliro reader: initializes the reader's UWB
adapter and Cherry CCC context once, then arms, feeds, and tears down per-connection ranging
sessions driven by the M1-M4 setup exchanged over the peer's L2CAP channel.
Maintains process-wide singletons for the Cherry context and adapter (set up once via
aliro_ranging_init) and for the single active ranging session (the DW3000 supports only one
session at a time), tracking its owning secure channel for send/receive framing.

**depends on** [`modules/woz_aliro/include/aliro_ble.h`](../modules.woz_aliro.include/aliro_ble.h.md), [`modules/woz_aliro/include/aliro_crypto.h`](../modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/src/aliro_ranging.h`](aliro_ranging.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](../modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](../modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](../modules.woz_uwb.src.facade/woz_uwb_facade.h.md), [`ports/esp32-idf/components/woz_uwb/compat/zephyr/logging/log.h`](../ports.esp32-idf.components.woz_uwb.compat.zephyr.logging/log.h.md)  ·  **discussed in** [`ports/esp32-idf/components/aliro_reader/README.md`](../../../ports/esp32-idf/components/aliro_reader/README.md)

## API

### `static struct aliro_uwb_adapter_reader_config s_reader_cfg =`
`modules/woz_aliro/src/aliro_ranging.c:42`

Reader-side selection preferences. BORROWED for the adapter's lifetime (the
adapter stores the pointer, not a copy), so this must have static storage.

### `static struct cherry *s_cherry`
`modules/woz_aliro/src/aliro_ranging.c:55`

Process-wide handle to the active Cherry CCC context, or NULL if ranging has not been set up.

### `static struct aliro_uwb_adapter *s_adapter`
`modules/woz_aliro/src/aliro_ranging.c:57`

Process-wide handle to the active Aliro UWB adapter, or NULL if ranging has not been set up.

### `static void uwb_tx_cb(struct aliro_uwb_message *message, struct aliro_uwb_session *session, void *user_data, bool timeout)`
`modules/woz_aliro/src/aliro_ranging.c:75`

Send an adapter-built message verbatim over the peer's L2CAP channel. The
bytes already carry the 4-byte Aliro header; hand them straight to the BLE
send. We own the message and MUST free it (even if we don't send).

### `static void uwb_ev_cb(struct aliro_uwb_session_event *event, void *user_data)`
`modules/woz_aliro/src/aliro_ranging.c:107`

Session notifications. On DEINIT the engine frees the session right after this
returns, so never touch event->session here; identify via user_data. Every
event must be freed.

### `static void uwb_ev_cb(struct aliro_uwb_session_event *event, void *user_data)`
`modules/woz_aliro/src/aliro_ranging.c:107`

Session notifications. On DEINIT the engine frees the session right after this
returns, so never touch event->session here; identify via user_data. Every
event must be freed.

### `int aliro_ranging_init(void)`
`modules/woz_aliro/src/aliro_ranging.c:139`

One-time bring-up of the reader UWB adapter (cherry ctx + capabilities +
reader config). Idempotent. Returns 0 on success, negative on failure (the
reader still runs; ranging just won't start).

### `struct cherry_ccc_capabilities ccc =`
`modules/woz_aliro/src/aliro_ranging.c:156`

CCC capabilities advertised to the Aliro UWB adapter for this reader.

### `struct cherry_core_event_device_capabilities caps =`
`modules/woz_aliro/src/aliro_ranging.c:169`

Device capabilities event reported by CCC, describing supported protocol versions, UWB configurations,
and pulse shape combinations.

### `const struct woz_uwb_aliro_cfg probe_cfg =`
`modules/woz_aliro/src/aliro_ranging.c:195`

Aliro UWB Kconfig-equivalent probe configuration used to bring up the woz_uwb layer on this port.

### `int aliro_ranging_start(uint16_t conn_handle, uint32_t session_id, const uint8_t *ursk, struct aliro_secchan *sc_ble)`
`modules/woz_aliro/src/aliro_ranging.c:211`

Arm the M1-M4 ranging setup for a connection: create the session with ranging
session id @session_id, bound to the 32-byte @ursk and the BleSK ranging channel
@sc_ble, whose reader-direction counter is used to seal the engine's outbound SDUs
(continuing from the AP-Completed message). @session_id MUST match the value the
device derived from the AUTH0 transaction id (big-endian txid[12..15]); it is
advertised in M1 and the device indexes its URSK by it. M1 is NOT sent here — the
engine emits it when the device sends its Initiate-Ranging-Session. Returns 0 on
success, negative on failure or if a ranging session is already active (the DW3000
is single-session).

### `static struct aliro_secchan *s_sc_ble`
`modules/woz_aliro/src/aliro_ranging.c:212`

The connection's BleSK ranging channel (owned by the reader session), used to
seal the engine's outbound SDUs. Borrowed for the session's lifetime.

### `int aliro_ranging_feed(uint16_t conn_handle, const uint8_t *data, size_t len)`
`modules/woz_aliro/src/aliro_ranging.c:255`

Feed one inbound post-auth PLAINTEXT SDU (already BleSK-opened by the reader;
proto/id/len header + payload) to the active ranging session. M4 makes the
engine start the responder with the negotiated parameters. Returns 0 if
consumed, negative if there is no active session or the engine rejected it.

### `static void uwb_tx_cb(struct aliro_uwb_message *message, struct aliro_uwb_session *session, void *user_data, bool timeout)`
`modules/woz_aliro/src/aliro_ranging.c:269`

Send an adapter-built message verbatim over the peer's L2CAP channel. The
bytes already carry the 4-byte Aliro header; hand them straight to the BLE
send. We own the message and MUST free it (even if we don't send).

### `void aliro_ranging_stop(uint16_t conn_handle)`
`modules/woz_aliro/src/aliro_ranging.c:286`

Tear down the ranging session for a connection (on disconnect). No-op if none
is active for @conn_handle.

### `static struct aliro_uwb_session *s_sess`
`modules/woz_aliro/src/aliro_ranging.c:291`

The single active ranging session (the DW3000 is single-session). Owned and
mutated only on the BLE-host task. s_sess is cleared when the engine frees the
session (a DEINIT status event) or when we tear it down.
