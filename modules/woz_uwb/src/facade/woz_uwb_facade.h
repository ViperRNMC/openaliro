// Public header for UWB facade: exposes Aliro DS-TWR responder lifecycle and range query; the CCC engine is bound and unbound via internal ursk and stop calls.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 * C shim bridging the add-on UWB impl to the Woz FiRa/CCC engine.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bind the CCC STS from the add-on-supplied plaintext URSK; returns 0 on success. */
int woz_uwb_bind_ursk(const uint8_t *ursk, size_t ursk_len);

/** Ranging parameters the Aliro M1-M4 handshake negotiated. */
struct woz_uwb_aliro_cfg {
	uint32_t session_id;        /**< Aliro UWB session id (any non-zero). */
	uint8_t channel;            /**< UWB channel: 5 or 9. */
	uint8_t sync_code_index;    /**< SYNC/preamble code index (1..32). */
	uint16_t slot_duration_rstu;/**< Slot duration, RSTU (1200 = 1 ms). */
	uint32_t block_duration_ms; /**< Ranging block period, ms. */
	uint8_t slot_per_round;     /**< Slots per ranging round. */
	uint32_t sts_index0;        /**< Starting STS index. */
	uint64_t uwb_time_us;       /**< UWB_Time0 initiation reference, µs. */
	const uint8_t *ursk;        /**< 32-byte URSK (provisioned STS root). */
	/** Serialized RangingConfiguration (CCC SaltedHash input), or NULL for URSK fallback. */
	const uint8_t *ranging_config;
	size_t rc_len;              /**< RangingConfiguration length, bytes (17). */
};

/** Start the CCC DS-TWR responder bound to a live Aliro credential; returns 0 on success. */
int woz_uwb_start_aliro(const struct woz_uwb_aliro_cfg *cfg);

/** Quiesce the radio and unbind the CCC STS shim. */
void woz_uwb_stop(void);

/** Latest distance in cm; true if a valid range has been seen. */
bool woz_uwb_last_range_cm(int32_t *cm_out);

#ifdef __cplusplus
}
#endif
