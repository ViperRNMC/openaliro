/** @file aliro_uwb_session.c — per-session lifecycle and state machine. */

#include "aliro_uwb_internal.h"
#include "aliro_uwb_msg.h"
#include "aliro_uwb_msg_spec.h"

#include <aliro_uwb_adapter/aliro_uwb_session.h>
#include <cherry/cherry_ccc.h>

#include "woz_alloc.h"
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(woz_aliro_uwb, LOG_LEVEL_INF);

#define ALIRO_UWB_URSK_SIZE 32

/**
 * @brief Send a general-error notification to the peer.
 * @param session Session on which to build and transmit the error message.
 * @return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL, or
 * `ALIRO_UWB_ERR_INTERNAL` if the message could not be built.
 */
static enum aliro_uwb_err notify_error(struct aliro_uwb_session *session)
{
	struct aliro_uwb_message *message;

	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	message = aliro_uwb_msg_build_general_error(session,
						    ALIRO_UWB_NOTIFICATION_GENERAL_ERROR_UNKNOWN);
	if (!message) {
		return ALIRO_UWB_ERR_INTERNAL;
	}
	session->transmit(message, session, session->user_data, false);
	return ALIRO_UWB_ERR_NONE;
}

/**
 * @brief CCC seam callback: wrap the CCC event and forward it to the client.
 * @param event CCC event to wrap and forward.
 * @param user_data Aliro UWB session that owns the callback and client data.
 */
static void aliro_ccc_cb(struct cherry_ccc_event *event, void *user_data)
{
	struct aliro_uwb_session *session = user_data;
	aliro_uwb_session_cb_t callback = session->callback;
	void *client_data = session->user_data;
	struct aliro_uwb_session_event *wrapped;

	wrapped = qmalloc(sizeof(*wrapped));
	if (!wrapped) {
		LOG_ERR("aliro_ccc_cb: OOM");
		return;
	}

	wrapped->session = session;
	switch (event->type) {
	case CHERRY_CCC_EVENT_TYPE_SESSION_STATUS:
		wrapped->type = ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_STATUS;
		wrapped->data.status = event->data.status;
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_ERROR:
		wrapped->type = ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_ERROR;
		wrapped->data.error = event->data.error;
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLLER_REPORT:
		wrapped->type = ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLLER_REPORT;
		wrapped->data.controller_report = event->data.controller_report;
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLEE_REPORT:
		wrapped->type = ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLEE_REPORT;
		wrapped->data.controlee_report = event->data.controlee_report;
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT:
		wrapped->type = ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT;
		wrapped->data.diagnostics = event->data.diagnostics;
		break;
	default:
		LOG_INF("aliro_ccc_cb: unknown event type %u", event->type);
		qfree(wrapped);
		return;
	}

	wrapped->cherry_event = event;

	if (event->type == CHERRY_CCC_EVENT_TYPE_SESSION_ERROR) {
		notify_error(session);
	}
	if (event->type == CHERRY_CCC_EVENT_TYPE_SESSION_STATUS &&
	    event->data.status->session_state == CHERRY_CCC_SESSION_STATE_DEINIT) {
		qfree(session);
	}

	callback(wrapped, client_data);
}

/**
 * @brief Tear down: destroy the CCC session, or free directly if there is none.
 * @param session Session to close.
 */
static void session_close(struct aliro_uwb_session *session)
{
	if (session->ccc_session) {
		cherry_ccc_session_destroy(session->ccc_session);
	} else {
		qfree(session);
	}
}

/**
 * @brief Initialize a session by creating and configuring a CCC Aliro responder, setting URSK,
 * protocol version, antennas, and diagnostics, then starting the session. On any error, tears down
 * the session and returns the mapped error code.
 * @param session Session to initialize.
 * @return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL, or
 * the mapped CCC error on failure.
 */
enum aliro_uwb_err aliro_uwb_session_init(struct aliro_uwb_session *session)
{
	/**
	 * @brief Reader configuration attached to an adapter, specifying hopping preferences and
	 * antenna assignments.
	 */
	struct aliro_uwb_adapter_reader_config *reader;
	enum cherry_err err;

	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}

	session->ccc_session = cherry_ccc_session_create_aliro_responder(
		session->aliro_ctx->cherry_ctx, aliro_ccc_cb, session, &session->ccc_aliro_config);
	if (!session->ccc_session) {
		LOG_ERR("create_aliro_responder failed");
		return ALIRO_UWB_ERR_INTERNAL;
	}

	if (session->ursk) {
		err = cherry_ccc_session_set_ursk(session->ccc_session, session->ursk);
		if (err) {
			goto fail;
		}
	}
	if (session->selected_protocol_version) {
		err = cherry_ccc_session_set_protocol_version(session->ccc_session,
							      session->selected_protocol_version);
		if (err) {
			goto fail;
		}
	}

	reader = session->aliro_ctx->config;
	if (reader->r1_antennas[0] || reader->r1_antennas[1]) {
		err = cherry_ccc_session_set_antennas(session->ccc_session, reader->r1_antennas[0],
						      reader->r1_antennas[1]);
		if (err) {
			goto fail;
		}
	}
	if (reader->r2_antennas[0] || reader->r2_antennas[1]) {
		err = cherry_ccc_session_set_round2_antennas(
			session->ccc_session, reader->r2_antennas[0], reader->r2_antennas[1]);
		if (err) {
			goto fail;
		}
	}
	if (session->aliro_ctx->diag_config) {
		err = cherry_ccc_session_set_diagnostics(session->ccc_session,
							 *session->aliro_ctx->diag_config);
		if (err) {
			goto fail;
		}
	}

	err = cherry_ccc_session_start(session->ccc_session);
	if (err) {
		goto fail;
	}
	return ALIRO_UWB_ERR_NONE;

fail:
	LOG_ERR("session init step failed: %d", err);
	session_close(session);
	return cherry_err_to_aliro(err);
}

/**
 * @brief Start an active CCC session. On error, tears down the session and returns the mapped error
 * code.
 * @param session Session to start.
 * @return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL, or
 * the mapped CCC error on failure.
 */
enum aliro_uwb_err aliro_uwb_session_start(struct aliro_uwb_session *session)
{
	enum cherry_err err;

	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	err = cherry_ccc_session_start(session->ccc_session);
	if (err) {
		session_close(session);
		return cherry_err_to_aliro(err);
	}
	return ALIRO_UWB_ERR_NONE;
}

/**
 * @brief Stop an active CCC session, transitioning to SUSPENDED state. On error, tears down the
 * session and returns the mapped error code.
 * @param session Session to stop.
 * @return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL, or
 * the mapped CCC error on failure.
 */
enum aliro_uwb_err aliro_uwb_session_stop(struct aliro_uwb_session *session)
{
	enum cherry_err err;

	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	err = cherry_ccc_session_stop(session->ccc_session);
	if (err) {
		session_close(session);
		return cherry_err_to_aliro(err);
	}
	session->state = SUSPENDED;
	return ALIRO_UWB_ERR_NONE;
}

struct aliro_uwb_session
	*
	/**
	 * @brief Opaque Aliro UWB adapter handle, holds CCC context and reader configuration for
	 * session setup.
	 */
	aliro_uwb_session_create(struct aliro_uwb_adapter *aliro_ctx, uint32_t session_id,
				 aliro_uwb_session_cb_t callback,
				 aliro_uwb_adapter_transmit_message_t transmit, void *user_data)
{
	struct aliro_uwb_session *session;

	if (!aliro_ctx || !transmit || !callback) {
		return NULL;
	}

	session = qcalloc(1, sizeof(*session));
	if (!session) {
		return NULL;
	}

	session->aliro_ctx = aliro_ctx;
	session->session_id = session_id;
	session->callback = callback;
	session->transmit = transmit;
	session->user_data = user_data;
	session->state = CREATED;

	LOG_INF("Aliro session created");
	return session;
}

/**
 * @brief Destroy an Aliro UWB session, freeing the URSK and tearing down the underlying CCC
 * session.
 * @param session Session to destroy; no-op if NULL.
 */
void aliro_uwb_session_destroy(struct aliro_uwb_session *session)
{
	if (!session) {
		return;
	}
	qfree(session->ursk);
	session_close(session);
}

/**
 * @brief Free a session message, delegating to the message-specific free function.
 * @param message Message to free.
 */
void aliro_uwb_session_message_free(struct aliro_uwb_message *message)
{
	aliro_uwb_msg_free(message);
}

/**
 * @brief Free a session event, releasing its wrapped CCC event if present.
 * @param event Event to free; no-op if NULL.
 */
void aliro_uwb_session_event_free(struct aliro_uwb_session_event *event)
{
	if (!event) {
		return;
	}
	if (event->cherry_event) {
		cherry_ccc_event_free(event->cherry_event);
	}
	qfree(event);
}

/**
 * @brief Store a copy of the URSK (Unique Ranging Session Key) for later use during session
 * initialization. Allocates a 16-byte buffer and returns ALIRO_UWB_ERR_INTERNAL on allocation
 * failure.
 * @param session Session that receives the copied URSK.
 * @param ursk Source URSK bytes to copy.
 * @return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session or ursk is
 * NULL, or `ALIRO_UWB_ERR_INTERNAL` on allocation failure.
 */
enum aliro_uwb_err aliro_uwb_session_set_ursk(struct aliro_uwb_session *session,
					      const uint8_t *ursk)
{
	if (!session || !ursk) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	session->ursk = qmalloc(ALIRO_UWB_URSK_SIZE);
	if (!session->ursk) {
		return ALIRO_UWB_ERR_INTERNAL;
	}
	memcpy(session->ursk, ursk, ALIRO_UWB_URSK_SIZE);
	return ALIRO_UWB_ERR_NONE;
}

enum aliro_uwb_err
/**
 * @brief Store the protocol version selected by the reader for later use during session initialization.
 * @param session Session that receives the selected protocol version.
 * @param selected_protocol_version Protocol version chosen by the reader.
 * @return `ALIRO_UWB_ERR_NONE` on success, or `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL.
 */
aliro_uwb_session_set_protocol_version(struct aliro_uwb_session *session,
				       uint16_t selected_protocol_version)
{
	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	session->selected_protocol_version = selected_protocol_version;
	return ALIRO_UWB_ERR_NONE;
}

/**
 * @brief Begin session setup by building and transmitting M1, transitioning from CREATED to M1_SENT
 * state. Returns ALIRO_UWB_ERR_INVALID_STATE if not in CREATED state.
 * @param session Session to begin setup on.
 * @return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL,
 * `ALIRO_UWB_ERR_INVALID_STATE` if not in CREATED state, or `ALIRO_UWB_ERR_INTERNAL` if M1 could
 * not be built.
 */
enum aliro_uwb_err aliro_uwb_session_init_setup(struct aliro_uwb_session *session)
{
	struct aliro_uwb_message *m1;

	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	if (session->state != CREATED) {
		LOG_ERR("init_setup in bad state %u", session->state);
		return ALIRO_UWB_ERR_INVALID_STATE;
	}

	/* Only session_id is known now; the rest is filled in as M2/M4 arrive. */
	session->ccc_aliro_config.session_id = session->session_id;

	m1 = aliro_uwb_msg_build_m1(session);
	if (!m1) {
		return ALIRO_UWB_ERR_INTERNAL;
	}
	LOG_INF("Sending RangingSessionSetupM1 message");
	session->transmit(m1, session, session->user_data, true);
	session->state = M1_SENT;
	return ALIRO_UWB_ERR_NONE;
}

enum aliro_uwb_err
/**
 * @brief Store the time offset used to synchronize clocks between reader and device.
 * @param session Session to update.
 * @param time_offset Time offset in microseconds.
 * @return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session is NULL.
 */
aliro_uwb_session_set_time_offset(struct aliro_uwb_session *session,
				  int64_t time_offset)
{
	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	session->time_offset = time_offset;
	return ALIRO_UWB_ERR_NONE;
}

enum aliro_uwb_err
/**
 * @brief Validate and dispatch an incoming Aliro UWB message to the appropriate protocol handler.
 * @param session Session that received the message.
 * @param message Message to validate and process.
 * @return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session or message is NULL, ALIRO_UWB_ERR_MSG_MALFORMED if the message is shorter than the header or the payload length does not match, ALIRO_UWB_ERR_MESSAGE_UNSUPPORTED for an unrecognized protocol.
 */
aliro_uwb_session_message_handle(struct aliro_uwb_session *session,
				 struct aliro_uwb_message *message)
{
	uint8_t protocol;
	uint16_t payload_length;

	if (!session || !message) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	if (message->len < ALIRO_HEADER_LENGTH) {
		LOG_ERR("message shorter than header");
		return ALIRO_UWB_ERR_MSG_MALFORMED;
	}

	protocol = aliro_uwb_msg_protocol_header(message->data);
	payload_length = aliro_uwb_msg_payload_length(message->data);
	if (payload_length != message->len - ALIRO_HEADER_LENGTH) {
		LOG_ERR("payload length %u != actual %zu", payload_length,
			message->len - ALIRO_HEADER_LENGTH);
		return ALIRO_UWB_ERR_MSG_MALFORMED;
	}

	switch (protocol) {
	case ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE:
		return aliro_uwb_msg_process_ranging(session, message);
	case ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION:
		return aliro_uwb_msg_process_notification(session, message);
	case ALIRO_UWB_PROTOCOL_TYPE_SUPPLEMENTARY_SERVICE:
		return aliro_uwb_msg_process_supplementary(session, message);
	default:
		LOG_INF("protocol %u unsupported", protocol);
		return ALIRO_UWB_ERR_MESSAGE_UNSUPPORTED;
	}
}

/**
 * @brief Suspend an active ranging session by sending a suspend request.
 * @param session Session to suspend.
 * @return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session is NULL,
 * ALIRO_UWB_ERR_INVALID_STATE if there is no active CCC session or the session is not in the
 * RANGING state, ALIRO_UWB_ERR_INTERNAL if the suspend request could not be built.
 */
enum aliro_uwb_err aliro_uwb_session_suspend(struct aliro_uwb_session *session)
{
	struct aliro_uwb_message *request;

	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	if (!session->ccc_session) {
		return ALIRO_UWB_ERR_INVALID_STATE;
	}
	if (session->state != RANGING) {
		LOG_ERR("suspend in bad state %u", session->state);
		return ALIRO_UWB_ERR_INVALID_STATE;
	}

	request = aliro_uwb_msg_build_suspend_resume_request(session, true);
	if (!request) {
		return ALIRO_UWB_ERR_INTERNAL;
	}
	session->transmit(request, session, session->user_data, true);
	session->state = SUSPEND_REQ_SENT;
	return ALIRO_UWB_ERR_NONE;
}

enum aliro_uwb_err
/**
 * @brief Forcibly stop the active CCC session, transitioning it to SUSPENDED without a request/response exchange.
 * @param session Session to force-suspend.
 * @return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session is NULL, ALIRO_UWB_ERR_INVALID_STATE if no CCC session is active, otherwise the error translated from cherry_ccc_session_stop.
 */
aliro_uwb_session_forced_suspend(struct aliro_uwb_session *session)
{
	enum cherry_err err;

	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	if (!session->ccc_session) {
		return ALIRO_UWB_ERR_INVALID_STATE;
	}
	err = cherry_ccc_session_stop(session->ccc_session);
	if (err == CHERRY_ERR_NONE) {
		session->state = SUSPENDED;
	}
	return cherry_err_to_aliro(err);
}

/**
 * @brief Resume a suspended ranging session by building and transmitting a resume request.
 * @param session Session to resume.
 * @return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session is NULL,
 * ALIRO_UWB_ERR_INVALID_STATE if there is no active CCC session or the session is not in the
 * SUSPENDED state, ALIRO_UWB_ERR_INTERNAL if the resume request could not be built.
 */
enum aliro_uwb_err aliro_uwb_session_resume(struct aliro_uwb_session *session)
{
	/**
	 * @brief Serialized Aliro UWB message (M1-M4 or notification) built for transmission,
	 * holding protocol header and payload.
	 */
	struct aliro_uwb_message *request;

	if (!session) {
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	}
	if (!session->ccc_session) {
		return ALIRO_UWB_ERR_INVALID_STATE;
	}
	if (session->state != SUSPENDED) {
		LOG_ERR("resume in bad state %u", session->state);
		return ALIRO_UWB_ERR_INVALID_STATE;
	}

	request = aliro_uwb_msg_build_suspend_resume_request(session, false);
	if (!request) {
		return ALIRO_UWB_ERR_INTERNAL;
	}
	session->transmit(request, session, session->user_data, true);
	session->state = RESUME_REQ_SENT;
	return ALIRO_UWB_ERR_NONE;
}
