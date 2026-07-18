/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_ble — Aliro BLE transport (NimBLE) for the ESP32-S3 port. Phase 2.
 * Advertises the Aliro GATT service, negotiates the BLE-UWB protocol version,
 * and (from 2.2) carries the Aliro transaction over an L2CAP CoC. Clean-room
 * reimplementation; see SPEC.md for the wire protocol and provenance.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Aliro BLE-UWB supported-features flags (advertised in the READ char, and
 *  parsed from the device WRITE). Serialized as one byte: bit0/1/2. */
struct aliro_ble_features {
	bool timesync_procedure_0;
	bool timesync_procedure_1;
	bool le_coded_phy;
};

/** Transport callbacks into the app / Phase-3 Aliro handler. All optional. */
struct aliro_ble_callbacks {
	/** An L2CAP SDU arrived from the peer (2.2+). */
	void (*on_data)(uint16_t conn_handle, const uint8_t *data, uint16_t len);
	/** L2CAP channel opened / closed for a peer (2.2+). */
	void (*on_connected)(uint16_t conn_handle);
	void (*on_disconnected)(uint16_t conn_handle);
};

/** Reader configuration. `proto_versions` are host-order uint16s; they are the
 *  provisioned `aliroSupportedBLEUWBProtocolVersions` (Matter attr 133), NOT a
 *  transport constant, so the caller supplies them. */
struct aliro_ble_config {
	const uint16_t *proto_versions;
	size_t proto_versions_count;
	struct aliro_ble_features features;
	struct aliro_ble_callbacks cb;
};

/** Start NimBLE, register the Aliro GATT service, and begin advertising.
 *  Returns 0 on success, negative errno otherwise. */
int aliro_ble_start(const struct aliro_ble_config *cfg);

/** The L2CAP SPSM published to peers in the READ characteristic. */
uint16_t aliro_ble_spsm(void);

/** Send an SDU to the peer over its L2CAP channel (2.2+). Returns 0 on success. */
int aliro_ble_send(uint16_t conn_handle, const uint8_t *data, size_t len);

/* ---- Attach mode: share a NimBLE host another stack already owns ---------- *
 * Instead of owning NimBLE (aliro_ble_start), the reader can attach to a host
 * brought up by e.g. esp-matter, so both coexist on one controller. Three
 * phases: prepare() captures the config; the owner registers our GATT service
 * (aliro_ble_service_def()) through its extra-services hook BEFORE it starts its
 * GATT server; start_attached() brings up the L2CAP CoC + advertising once the
 * host is synced and the owner has released the advertiser (post-commissioning). */
struct ble_gatt_svc_def; /* NimBLE type, opaque here */

/** Capture config + build the READ payload; does NOT touch NimBLE. 0 on ok. */
int aliro_ble_prepare(const struct aliro_ble_config *cfg);

/** The Aliro GATT service definition, to hand to the host owner's
 *  register-extra-services hook. Valid after aliro_ble_prepare(). */
const struct ble_gatt_svc_def *aliro_ble_service_def(void);

/** Bring up the reader on the already-synced shared host: L2CAP CoC +
 *  advertising. Returns 0 on success. */
int aliro_ble_start_attached(void);

/** Set the provisioned Aliro advertising params (BLE-UWB approach discovery): the
 *  truncated reader group id (8) + sub id (2), the group resolving key (16) for
 *  the dynamic tag, and the tx-power byte. Call before start_attached(); once set,
 *  the reader advertises the full resolvable 0xFFF2 service data instead of the
 *  bare service UUID, so the phone can approach-connect. */
void aliro_ble_set_adv_params(const uint8_t group_id8[8], const uint8_t sub_id2[2],
			      const uint8_t grk[16], int8_t tx_power);

/** Re-emit the advertisement with the current adv params (call after
 *  aliro_ble_set_adv_params updates the GRK post-provisioning). No-op until
 *  start_attached() has brought the advertiser up. */
void aliro_ble_readvertise(void);

#ifdef __cplusplus
}
#endif
