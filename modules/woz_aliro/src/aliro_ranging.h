// Aliro M1-M4 ranging-setup interface: negotiates UWB ranging parameters with the device and
// produces the BLE ranging-control secure channel used to carry the M1-M4 exchange.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_ranging — the post-auth Aliro UWB ranging-setup (M1-M4) on ESP32. Thin
 * glue that drives the engine's reader adapter/session (modules/woz_uwb aliro_uwb_
 * adapter + session): on a completed credential-auth it creates a session bound to
 * the derived URSK, emits M1 over the BLE L2CAP channel, feeds inbound M2/M4 back
 * to the engine, and lets the engine negotiate the ranging parameters and start
 * the DW3000 responder itself (via cherry_ccc_shim -> woz_uwb_start_aliro). This
 * replaces the earlier canned-parameter handoff.
 *
 * The DW3000 is single-session, so exactly one ranging session runs at a time.
 * The whole lifecycle stays on the BLE-host task (create/feed/teardown + the
 * engine's transmit/event callbacks are all synchronous), so no locking is needed.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One-time bring-up of the reader UWB adapter (cherry ctx + capabilities +
 *  reader config). Idempotent. Returns 0 on success, negative on failure (the
 *  reader still runs; ranging just won't start). */
int aliro_ranging_init(void);

struct aliro_secchan; /* the BleSK ranging channel (from aliro_crypto.h) */

/** Arm the M1-M4 ranging setup for a connection: create the session with ranging
 *  session id @session_id, bound to the 32-byte @ursk and the BleSK ranging channel
 *  @sc_ble, whose reader-direction counter is used to seal the engine's outbound SDUs
 *  (continuing from the AP-Completed message). @session_id MUST match the value the
 *  device derived from the AUTH0 transaction id (big-endian txid[12..15]); it is
 *  advertised in M1 and the device indexes its URSK by it. M1 is NOT sent here — the
 *  engine emits it when the device sends its Initiate-Ranging-Session. Returns 0 on
 *  success, negative on failure or if a ranging session is already active (the DW3000
 *  is single-session). */
int aliro_ranging_start(uint16_t conn_handle, uint32_t session_id, const uint8_t *ursk,
			// Output secure channel for the BLE ranging control channel (M1-M4),
			// populated alongside the AP secure channel during Aliro authentication.
			struct aliro_secchan *sc_ble);

/** Feed one inbound post-auth PLAINTEXT SDU (already BleSK-opened by the reader;
 *  proto/id/len header + payload) to the active ranging session. M4 makes the
 *  engine start the responder with the negotiated parameters. Returns 0 if
 *  consumed, negative if there is no active session or the engine rejected it. */
int aliro_ranging_feed(uint16_t conn_handle, const uint8_t *data, size_t len);

/** Tear down the ranging session for a connection (on disconnect). No-op if none
 *  is active for @conn_handle. */
void aliro_ranging_stop(uint16_t conn_handle);

#ifdef __cplusplus
}
#endif
