/** @file aliro_uwb_msg.h — message framing accessors, dispatch and builders. */

#pragma once

#include "aliro_uwb_internal.h"

#include <aliro_uwb_adapter/aliro_uwb_session.h>

#include <stdint.h>

/* Header field accessors (operate on the raw message bytes). */
uint8_t aliro_uwb_msg_protocol_header(const uint8_t *bytes);
uint8_t aliro_uwb_msg_message_id(const uint8_t *bytes);
uint16_t aliro_uwb_msg_payload_length(const uint8_t *bytes);

/* Protocol dispatch, called from aliro_uwb_session_message_handle(). */
enum aliro_uwb_err aliro_uwb_msg_process_ranging(struct aliro_uwb_session *session,
						 struct aliro_uwb_message *message);
enum aliro_uwb_err aliro_uwb_msg_process_notification(struct aliro_uwb_session *session,
						      struct aliro_uwb_message *message);

/* Message builders (used by the session lifecycle). */
struct aliro_uwb_message *aliro_uwb_msg_build_m1(struct aliro_uwb_session *session);
struct aliro_uwb_message *
aliro_uwb_msg_build_suspend_resume_request(struct aliro_uwb_session *session, bool suspend);
struct aliro_uwb_message
	*
	/**
	 * @brief Build a general-error message for the given session.
	 * @param session Session for which the error message is built.
	 */
	aliro_uwb_msg_build_general_error(struct aliro_uwb_session *session, uint8_t error_code);

/**
 * @brief Release a message built by this layer.
 * @param message Message to free.
 */
void aliro_uwb_msg_free(struct aliro_uwb_message *message);
