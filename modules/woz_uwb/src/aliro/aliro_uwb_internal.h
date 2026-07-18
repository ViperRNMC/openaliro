/** @file aliro_uwb_internal.h — private context types and shared helpers. */

#pragma once

#include <aliro_uwb_adapter/aliro_uwb_adapter.h>
#include <aliro_uwb_adapter/aliro_uwb_session.h>
#include <cherry/cherry.h>
#include <cherry/cherry_ccc.h>

#include <stdint.h>

/**
 * @brief Session-independent reader state shared across all ranging sessions for an Aliro UWB
 * adapter.
 */
struct aliro_uwb_adapter {
	/**
	 * @brief Cherry library context managing CCC session lifecycle and event dispatch.
	 */
	struct cherry *cherry_ctx;
	/**
	 * @brief Reader adapter configuration supplied by the caller (e.g., ranging parameters,
	 * capabilities).
	 */
	struct aliro_uwb_adapter_reader_config *config;
	/**
	 * @brief CCC device capabilities (supported channels, PRF, ranging mode) discovered during
	 * adapter initialization.
	 */
	struct cherry_ccc_capabilities ccc_caps;
	uint8_t min_ran_multiplier;
	/**
	 * @brief Diagnostic configuration for CCC session reporting (e.g., ranging data, signal
	 * strength, diagnostics sampling).
	 */
	struct cherry_common_diag_cfg *diag_config;
};

/** BLE-handshake state of a session. */
enum aliro_uwb_session_state {
	CREATED,
	M1_SENT,
	M3_SENT,
	RANGING,
	SUSPEND_REQ_SENT,
	SUSPENDED,
	RESUME_REQ_SENT,
};

/** Per-approach ranging-setup session. */
struct aliro_uwb_session {
	/**
	 * @brief Session-independent reader state shared by this per-approach session.
	 */
	struct aliro_uwb_adapter *aliro_ctx;
	aliro_uwb_session_cb_t callback;
	aliro_uwb_adapter_transmit_message_t transmit;
	void *user_data;
	uint32_t session_id;
	uint8_t *ursk;
	int64_t time_offset;
	uint16_t selected_protocol_version;
	/**
	 * @brief Cherry CCC session object managing DS-TWR state and M1-M4 message handling for
	 * this ranging session.
	 */
	struct cherry_ccc_session *ccc_session;
	/**
	 * @brief CCC Aliro session configuration encoding the M1-M4 setup parameters (MAC, time
	 * sync, STS seed, hopping sequence).
	 */
	struct cherry_ccc_aliro_session_config ccc_aliro_config;
	enum aliro_uwb_session_state state;
};

/* Shared between the session and message-handling sources. */
enum aliro_uwb_err cherry_err_to_aliro(enum cherry_err err);
enum aliro_uwb_err aliro_uwb_session_init(struct aliro_uwb_session *session);
enum aliro_uwb_err aliro_uwb_session_stop(struct aliro_uwb_session *session);
/**
 * @brief Start ranging for a per-approach session.
 * @param session The per-approach ranging-setup session to start.
 * @return Error code indicating whether the session started successfully.
 */
enum aliro_uwb_err aliro_uwb_session_start(struct aliro_uwb_session *session);
