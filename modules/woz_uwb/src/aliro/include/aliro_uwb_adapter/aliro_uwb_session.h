/** @file aliro_uwb_session.h — per-session public interface. */

#pragma once

#include <cherry/cherry.h>
#include <cherry/cherry_ccc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque per-session context. */
struct aliro_uwb_session;

/** Framed Aliro BLE message (header + TLV payload). */
struct aliro_uwb_message {
	/** Number of valid bytes in @data. */
	size_t len;
	/** Message bytes (4-byte header followed by TLV attributes). */
	uint8_t data[];
};

/** Session event kinds (aliased to the CCC event types). */
enum aliro_uwb_session_event_type {
	ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_STATUS =
		CHERRY_CCC_EVENT_TYPE_SESSION_STATUS,
	ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_ERROR =
		CHERRY_CCC_EVENT_TYPE_SESSION_ERROR,
	ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLLER_REPORT =
		CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLLER_REPORT,
	ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLEE_REPORT =
		CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLEE_REPORT,
	ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT =
		CHERRY_CCC_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT,
};

/** Session event handed to the client. */
struct aliro_uwb_session_event {
	/**
	 * @brief Opaque per-session context.
	 */
	struct aliro_uwb_session *session;
	enum aliro_uwb_session_event_type type;
	void *cherry_event;
	union {
		/**
		 * @brief Session status report (active, paused, stopped) and update reason (initiation, measurement update, termination).
		 */
		struct cherry_ccc_session_event_session_status *status;
		/**
		 * @brief Session error payload carrying error code and diagnostic context.
		 */
		struct cherry_ccc_session_event_error *error;
		/**
		 * @brief Controller (phone) session report carrying its distance and timestamp estimate.
		 */
		struct cherry_ccc_controller_session_report *controller_report;
		/**
		 * @brief Controlee (lock) session report carrying its distance and timestamp estimate.
		 */
		struct cherry_ccc_controlee_session_report *controlee_report;
		/**
		 * @brief Diagnostic report snapshot (ranging samples, signal strength, time sync) for this update.
		 */
		struct cherry_common_diag_report *diagnostics;
	} data;
};

/** Session notification callback type. */
typedef void (*aliro_uwb_session_cb_t)(struct aliro_uwb_session_event *event,
				       void *user_data);

/** BLE transmit callback: sends an adapter-built message to the peer. */
typedef void (*aliro_uwb_adapter_transmit_message_t)(
	struct aliro_uwb_message *message, struct aliro_uwb_session *session,
	void *user_data, bool timeout);

/** Create a session (NULL on error). */
struct aliro_uwb_session *
/**
 * @brief Aliro adapter instance holding Cherry context, provisioned credentials, and ranging session state.
 * @param aliro_ctx Aliro adapter instance to create the session from.
 */
aliro_uwb_session_create(struct aliro_uwb_adapter *aliro_ctx,
			 uint32_t session_id, aliro_uwb_session_cb_t callback,
			 aliro_uwb_adapter_transmit_message_t transmit,
			 void *user_data);

/** Stop if needed and release a session. */
void aliro_uwb_session_destroy(struct aliro_uwb_session *session);

/** Free a message produced by the adapter's transmit callback. */
void aliro_uwb_session_message_free(struct aliro_uwb_message *message);

/**
 * @brief Free an event delivered to the session callback.
 * @param event Event to free.
 */
void aliro_uwb_session_event_free(struct aliro_uwb_session_event *event);

/** Provide the 32-byte URSK for the session. */
enum aliro_uwb_err aliro_uwb_session_set_ursk(struct aliro_uwb_session *session,
					      const uint8_t *ursk);

/** Record the selected BLE/UWB protocol version. */
enum aliro_uwb_err
aliro_uwb_session_set_protocol_version(struct aliro_uwb_session *session,
				       uint16_t selected_protocol_version);

/** Begin setup: build M1 and transmit it to the user device. */
enum aliro_uwb_err aliro_uwb_session_init_setup(struct aliro_uwb_session *session);

/** Set the time offset (microseconds) added to the negotiated UWB_Time0. */
enum aliro_uwb_err
aliro_uwb_session_set_time_offset(struct aliro_uwb_session *session,
				  int64_t time_offset);

/** Handle an incoming BLE message (M2/M4, suspend/resume, notifications). */
enum aliro_uwb_err
aliro_uwb_session_message_handle(struct aliro_uwb_session *session,
				 /**
				  * @brief Framed Aliro BLE message (header + TLV payload).
				  * @param message Framed Aliro BLE message to process.
				  */
				 struct aliro_uwb_message *message);

/** Request a graceful suspend (builds and sends a suspend request). */
enum aliro_uwb_err aliro_uwb_session_suspend(struct aliro_uwb_session *session);

/** Suspend ranging immediately without waiting for a peer response. */
enum aliro_uwb_err
aliro_uwb_session_forced_suspend(struct aliro_uwb_session *session);

/**
 * @brief Build and send a resume request for a suspended Aliro UWB ranging session.
 * @param session Aliro UWB session to resume.
 * @return Aliro UWB error code indicating success or failure of the resume request.
 */
enum aliro_uwb_err aliro_uwb_session_resume(struct aliro_uwb_session *session);

#ifdef __cplusplus
}
#endif
