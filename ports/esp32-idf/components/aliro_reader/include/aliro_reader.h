/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_reader — Aliro reader session/transaction layer (Phase 2.3+). Owns the
 * per-connection Aliro transaction on top of the aliro_ble transport: session
 * lifecycle, inbound message dispatch, diagnostics, and the hook that hands a
 * derived credential (URSK + ranging params) to the UWB engine.
 *
 * The M1-M4 cryptographic handshake and URSK derivation are Phase 3 and are
 * NOT implemented here; this layer observes/logs the transaction and marks the
 * exact seam where the crypto lands. See components/aliro_ble/SPEC.md.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up the Aliro reader (starts the BLE transport + session layer).
 *  Returns 0 on success, negative on failure. */
int aliro_reader_start(void);

/* ---- Attach mode: coexist with a host another stack owns (esp-matter) ----- *
 * Two phases so the reader shares one BLE controller with Matter:
 * aliro_reader_ble_prepare() runs BEFORE the host stack starts its GATT server
 * and returns the Aliro GATT service definition to register via that stack's
 * hook; aliro_reader_start_attached() runs once the host is up + the device is
 * operational (the owner has released the advertiser). */

/** Prepare the reader for attach mode + return the Aliro GATT service def to
 *  register (cast to `const struct ble_gatt_svc_def *`). NULL on failure. */
const void *aliro_reader_ble_prepare(void);

/** Start the reader on the shared host (L2CAP CoC + advertising + engine).
 *  Returns 0 on success, negative on failure. */
int aliro_reader_start_attached(void);

/** Re-emit the BLE advertisement using the currently-provisioned GRK. Call after
 *  Matter provisioning (SetAliroReaderConfig) if the reader may already be
 *  advertising: it starts on kCommissioningComplete, before Apple sends the Aliro
 *  config, so its first advertisement has no GRK and the phone cannot resolve it.
 *  No-op if the reader has no GRK or is not yet advertising. */
void aliro_reader_refresh_adv(void);

/* ---- Bench provisioning helpers (Phase 3.4) ---------------------------- *
 * Back the `aliro-prov` / `aliro-trust` console commands. Kept as plain calls
 * so the shell does not need the internal aliro_prov types. */

/** Print the reader identity (dev vs provisioned, reader_id), the trust store,
 *  and the most-recently-presented credential key. */
void aliro_reader_prov_print(void);

/** Trust the most-recently-presented credential public key and persist it to
 *  NVS. Returns 0 (added + saved), 1 (nothing presented yet, or already
 *  trusted), negative on a store error. */
int aliro_reader_trust_last(void);

/* ---- Matter provisioning bridge (Phase 4) ------------------------------ *
 * Apple Home provisions the reader over Matter (Door Lock SetAliroReaderConfig +
 * SetCredential). These let the Matter delegate persist that identity + trust
 * into the same NVS store the reader loads at start(), so a handoff-started
 * reader authenticates the Wallet credential Apple just installed. Kept as plain
 * calls (no aliro_prov types) so the C++ delegate needs only this header. */

/** Store the reader identity provisioned over Matter and persist it to NVS:
 *  reader_id = groupIdentifier(16) || groupSubIdentifier(16),
 *  sign_priv = signingKey(32), grk = groupResolvingKey(16) for the BLE-UWB
 *  advertising dynamic tag (pass all-zero if none); clears the dev flag. Existing
 *  trust anchors are preserved. Returns 0 on success, negative on an NVS error. */
int aliro_reader_provision_identity(const uint8_t reader_id[32], const uint8_t sign_priv[32],
				    const uint8_t grk[16]);

/** Add a trusted credential public key (uncompressed P-256, 65 bytes) presented
 *  over Matter SetCredential and persist. Returns 0 (added), 1 (already
 *  present), negative (store full / not a P-256 point / NVS error). */
int aliro_reader_provision_add_trust(const uint8_t cred_pub[65]);

/** Revert to the dev identity + empty trust store (Matter ClearAliroReaderConfig)
 *  and persist. Returns 0 on success, negative on an NVS error. */
int aliro_reader_provision_clear(void);

#ifdef __cplusplus
}
#endif
