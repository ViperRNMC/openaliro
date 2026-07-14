/** @file cherry_ccc.h — CCC/Aliro-session interface (seam the adapter drives). */

#pragma once

#include <cherry/cherry.h>
#include <cherry/cherry_common.h>
#include <cherry/cherry_session.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Length in bytes of the hopping-sequence key carried in M4. */
#define CHERRY_CCC_HOP_MODE_KEY_SIZE 4

/** Opaque CCC session (defined by the shim). */
struct cherry_ccc_session;

/**
 * @brief Device CCC/Aliro capability set advertised to the peer.
 */
struct cherry_ccc_capabilities {
	/** Supported slot durations, one bit per NChap-per-slot value. */
	uint8_t slot_bitmask;
	/** Usable SYNC code indices, bit i => code (i+1). */
	uint32_t sync_code_index_bitmask;
	/** Supported hopping modes/sequences bitmask. */
	uint8_t hopping_config_bitmask;
	/** Supported channels: b0 => ch5, b1 => ch9. */
	uint8_t channel_bitmask;
	/** Supported protocol versions (1.0 encoded 0x0100). */
	struct {
		size_t len;
		uint16_t *items;
	} protocol_versions;
	/** Supported UWB configuration identifiers. */
	struct {
		size_t len;
		uint16_t *items;
	} uwb_configs;
	/** Supported pulse-shape combinations. */
	struct {
		size_t len;
		uint8_t *items;
	} pulse_shape_combos;
	/** Smallest RAN multiplier the device accepts. */
	uint8_t minimum_ran_multiplier;
	/** Whether the optional vendor feature 1 is supported. */
	bool qorvo_vendor_feature_1_supported;
};

/** CCC session event kinds. */
enum cherry_ccc_event_type {
	CHERRY_CCC_EVENT_TYPE_SESSION_STATUS,
	CHERRY_CCC_EVENT_TYPE_SESSION_ERROR,
	CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLLER_REPORT,
	CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLEE_REPORT,
	CHERRY_CCC_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT,
};

/** Lifecycle state of a CCC session. */
enum cherry_ccc_session_state {
	CHERRY_CCC_SESSION_STATE_INIT,
	CHERRY_CCC_SESSION_STATE_DEINIT,
	CHERRY_CCC_SESSION_STATE_ACTIVE,
	CHERRY_CCC_SESSION_STATE_IDLE,
};

/** Why a session changed state. */
enum cherry_ccc_state_change_reason {
	CHERRY_CCC_STATE_CHANGE_REASON_MGMT_CMD,
	CHERRY_CCC_STATE_CHANGE_REASON_UNKNOWN,
	CHERRY_CCC_STATE_CHANGE_REASON_FORCE_STOPPED,
};

/**
 * @brief Payload of a SESSION_STATUS event, giving the new session state and the reason for the change.
 */
struct cherry_ccc_session_event_session_status {
	enum cherry_ccc_session_state session_state;
	enum cherry_ccc_state_change_reason reason_code;
};

/**
 * @brief Payload of a SESSION_ERROR event, giving the error status that triggered it.
 */
struct cherry_ccc_session_event_error {
	enum cherry_err status_err;
};

/**
 * @brief Opaque controller-side CCC session report; instances cross the API boundary only by pointer.
 */
struct cherry_ccc_controller_session_report;
/**
 * @brief Opaque CCC controlee (lock) session report structure, not defined outside the cherry library.
 */
struct cherry_ccc_controlee_session_report;

/** CCC notification delivered to the adapter. */
struct cherry_ccc_event {
	enum cherry_ccc_event_type type;
	/**
	 * @brief Opaque CCC session the event pertains to, defined by the shim.
	 */
	struct cherry_ccc_session *session;
	union {
		/**
		 * @brief Payload pointer for a SESSION_STATUS event.
		 */
		struct cherry_ccc_session_event_session_status *status;
		/**
		 * @brief Payload pointer for a SESSION_ERROR event.
		 */
		struct cherry_ccc_session_event_error *error;
		/**
		 * @brief Pointer to the controller-side session report payload; report payloads cross the seam only by pointer on this lock.
		 */
		struct cherry_ccc_controller_session_report *controller_report;
		/**
		 * @brief Pointer to the controlee session report payload, carrying a distance and timestamp snapshot.
		 */
		struct cherry_ccc_controlee_session_report *controlee_report;
		/**
		 * @brief Pointer to the diagnostic report snapshot (ranging samples, signal metrics) for this CCC event.
		 */
		struct cherry_common_diag_report *diagnostics;
	} data;
};

/** CCC notification callback type. */
typedef void (*cherry_ccc_cb_t)(struct cherry_ccc_event *event,
				void *user_data);

/** Selected hopping mode. */
enum cherry_ccc_hopping_mode {
	CHERRY_CCC_HOPPING_MODE_DISABLE = 0,
	CHERRY_CCC_HOPPING_MODE_ENABLED = 1,
	CHERRY_CCC_HOPPING_MODE_CONTINUOUS_AES = 0xA0,
	CHERRY_CCC_HOPPING_MODE_CONTINUOUS_DEFAULT = 0xA1,
	CHERRY_CCC_HOPPING_MODE_ADAPTATIVE_AES = 0xA2,
	CHERRY_CCC_HOPPING_MODE_ADAPTATIVE_DEFAULT = 0xA3,
};

/** Negotiated Aliro ranging params (filled in-place across M1-M4). */
struct cherry_ccc_aliro_session_config {
	uint32_t session_id;
	uint16_t uwb_config_id;
	uint8_t pulse_shape_combo;
	uint8_t channel;
	uint8_t sync_code_index;
	uint32_t ranging_duration_ms;
	uint8_t slot_per_rr;
	uint16_t slot_duration;
	enum cherry_ccc_hopping_mode hopping_mode;
	uint8_t hopping_config_bitmask;
	uint8_t hop_mode_key[CHERRY_CCC_HOP_MODE_KEY_SIZE];
	uint32_t sts_index;
	uint8_t mac_mode;
	uint64_t uwb_time_us;
};

/** Create an Aliro responder session bound to @config (NULL on error). */
struct cherry_ccc_session *cherry_ccc_session_create_aliro_responder(
	/**
	 * @brief Cherry library context managing CCC session lifecycle and event dispatch.
	 */
	struct cherry *ctx, cherry_ccc_cb_t callback, void *user_data,
	/**
	 * @brief Negotiated Aliro ranging parameters, filled in-place across M1-M4.
	 */
	struct cherry_ccc_aliro_session_config *config);

/**
 * @brief Return the base session for a CCC session (the base is the first member).
 */
struct cherry_session *
cherry_ccc_session_to_base(struct cherry_ccc_session *session);

/** Convenience: fetch the CCC session's user_data via its base. */
static inline void *
/**
 * @brief Fetch the CCC session's user_data via its base session.
 * @param session CCC session to query.
 * @return The user_data pointer associated with the session's base.
 */
cherry_ccc_session_get_user_data(struct cherry_ccc_session *session)
{
	return cherry_session_get_user_data(
		cherry_ccc_session_to_base(session));
}

/**
 * @brief Release a CCC event and any heap payload it owns.
 * @param event Event to free.
 */
void cherry_ccc_event_free(struct cherry_ccc_event *event);

/** Destroy a CCC session (delegates to the base). */
static inline void
/**
 * @brief Destroy a CCC session, delegating to the base session.
 * @param session CCC session to destroy.
 */
cherry_ccc_session_destroy(struct cherry_ccc_session *session)
{
	cherry_session_destroy(cherry_ccc_session_to_base(session));
}

/** Start a CCC session (delegates to the base). */
static inline enum cherry_err
/**
 * @brief Start a CCC session, delegating to the base session.
 * @param session CCC session to start.
 * @return Error code from cherry_session_start.
 */
cherry_ccc_session_start(struct cherry_ccc_session *session)
{
	return cherry_session_start(cherry_ccc_session_to_base(session));
}

/** Stop a CCC session (delegates to the base). */
static inline enum cherry_err
/**
 * @brief Stop a CCC session, delegating to the base session.
 * @param session CCC session to stop.
 * @return Error code from cherry_session_stop.
 */
cherry_ccc_session_stop(struct cherry_ccc_session *session)
{
	return cherry_session_stop(cherry_ccc_session_to_base(session));
}

/** Select round-1 antennas for a CCC session (delegates to the base). */
static inline enum cherry_err
/**
 * @brief Select round-1 antennas for a CCC session, delegating to the base session.
 * @param session CCC session to configure.
 * @param tx_antenna_set Antenna set to use for transmission.
 * @param rx_antenna_set Antenna set to use for reception.
 * @return Error code from cherry_session_set_antennas.
 */
cherry_ccc_session_set_antennas(struct cherry_ccc_session *session,
				uint8_t tx_antenna_set, uint8_t rx_antenna_set)
{
	return cherry_session_set_antennas(cherry_ccc_session_to_base(session),
					   tx_antenna_set, rx_antenna_set);
}

/** Provide the 32-byte URSK (host-provided STS root key). */
enum cherry_err cherry_ccc_session_set_ursk(struct cherry_ccc_session *session,
					    const uint8_t *ursk);

/** Record the selected BLE/UWB protocol version. */
enum cherry_err
cherry_ccc_session_set_protocol_version(struct cherry_ccc_session *session,
					uint16_t selected_protocol_version);

/** Select antennas for the Aliro second ranging round. */
enum cherry_err
cherry_ccc_session_set_round2_antennas(struct cherry_ccc_session *session,
				       uint8_t tx_antenna_set,
				       uint8_t rx_antenna_set);

/** Set the starting STS index (resume path). */
enum cherry_err
cherry_ccc_session_set_sts_index(struct cherry_ccc_session *session,
				 uint32_t sts_index);

/** Set the ranging initiation time on the UWBS time base (resume path). */
enum cherry_err
cherry_ccc_session_set_initiation_time(struct cherry_ccc_session *session,
				       uint64_t initiation_time_us);

/** Enable/disable diagnostics for a CCC session (delegates to the base). */
static inline enum cherry_err
/**
 * @brief Opaque CCC session being configured, defined by the shim.
 */
cherry_ccc_session_set_diagnostics(struct cherry_ccc_session *session,
				   /**
				    * @brief Diagnostic configuration for CCC session reporting (e.g., sampling interval, metrics to include).
				    */
				   struct cherry_common_diag_cfg config)
{
	return cherry_session_set_diagnostics(
		cherry_ccc_session_to_base(session), config, false);
}

#ifdef __cplusplus
}
#endif
