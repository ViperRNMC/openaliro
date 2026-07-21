<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/aliro_uwb_session.c`

@file aliro_uwb_session.c — per-session lifecycle and state machine.

**depends on** [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_spec.h`](aliro_uwb_msg_spec.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](../modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](../modules.woz_uwb.src.facade/woz_alloc.h.md)

```mermaid
flowchart TD
  aliro_ccc_cb --> notify_error
```

## API

### `static enum aliro_uwb_err notify_error(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:25`

@brief Send a general-error notification to the peer.
@param session Session on which to build and transmit the error message.
@return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL, or
`ALIRO_UWB_ERR_INTERNAL` if the message could not be built.

**called by** `aliro_ccc_cb`

### `static void aliro_ccc_cb(struct cherry_ccc_event *event, void *user_data)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:46`

@brief CCC seam callback: wrap the CCC event and forward it to the client.
@param event CCC event to wrap and forward.
@param user_data Aliro UWB session that owns the callback and client data.

**calls** `notify_error`

### `static void session_close(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:104`

@brief Tear down: destroy the CCC session, or free directly if there is none.
@param session Session to close.

**called by** `aliro_uwb_session_destroy`, `aliro_uwb_session_init`, `aliro_uwb_session_start`, `aliro_uwb_session_stop`

### `enum aliro_uwb_err aliro_uwb_session_init(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:121`

@brief Initialize a session by creating and configuring a CCC Aliro responder, setting URSK,
protocol version, antennas, and diagnostics, then starting the session. On any error, tears down
the session and returns the mapped error code.
@param session Session to initialize.
@return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL, or
the mapped CCC error on failure.

**calls** `session_close`

### `enum aliro_uwb_err aliro_uwb_session_start(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:191`

@brief Start an active CCC session. On error, tears down the session and returns the mapped error
code.
the mapped CCC error on failure.

**calls** `session_close`

### `enum aliro_uwb_err aliro_uwb_session_stop(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:213`

@brief Stop an active CCC session, transitioning to SUSPENDED state. On error, tears down the
session and returns the mapped error code.
@param session Session to stop.
@return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL, or
the mapped CCC error on failure.

**calls** `session_close`

### `struct aliro_uwb_session *aliro_uwb_session_create(struct aliro_uwb_adapter *aliro_ctx, uint32_t session_id, aliro_uwb_session_cb_t callback, aliro_uwb_adapter_transmit_message_t transmit, void *user_data)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:233`

@brief Allocate an Aliro UWB session in the CREATED state, bound to an adapter and to the
caller's transmit and event callbacks. No CCC session is started here.

### `void aliro_uwb_session_destroy(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:266`

@brief Destroy an Aliro UWB session, freeing the URSK and tearing down the underlying CCC
session.
@param session Session to destroy; no-op if NULL.

**calls** `session_close`

### `void aliro_uwb_session_message_free(struct aliro_uwb_message *message)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:279`

@brief Free a session message, delegating to the message-specific free function.
@param message Message to free.

### `void aliro_uwb_session_event_free(struct aliro_uwb_session_event *event)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:287`

@brief Free a session event, releasing its wrapped CCC event if present.

### `enum aliro_uwb_err aliro_uwb_session_set_ursk(struct aliro_uwb_session *session, const uint8_t *ursk)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:307`

@brief Store a copy of the URSK (Unique Ranging Session Key) for later use during session
initialization. Allocates a 16-byte buffer and returns ALIRO_UWB_ERR_INTERNAL on allocation
failure.
@param session Session that receives the copied URSK.
@param ursk Source URSK bytes to copy.
@return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session or ursk is
NULL, or `ALIRO_UWB_ERR_INTERNAL` on allocation failure.

### `enum aliro_uwb_err aliro_uwb_session_set_protocol_version(struct aliro_uwb_session *session, uint16_t selected_protocol_version)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:328`

@brief Store the protocol version selected by the reader for later use during session
initialization.
@param session Session that receives the selected protocol version.
@param selected_protocol_version Protocol version chosen by the reader.
@return `ALIRO_UWB_ERR_NONE` on success, or `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL.

### `enum aliro_uwb_err aliro_uwb_session_init_setup(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:346`

@brief Begin session setup by building and transmitting M1, transitioning from CREATED to M1_SENT
state. Returns ALIRO_UWB_ERR_INVALID_STATE if not in CREATED state.
@param session Session to begin setup on.
@return `ALIRO_UWB_ERR_NONE` on success, `ALIRO_UWB_ERR_INVALID_PARAMETER` if session is NULL,
`ALIRO_UWB_ERR_INVALID_STATE` if not in CREATED state, or `ALIRO_UWB_ERR_INTERNAL` if M1 could
not be built.

### `enum aliro_uwb_err aliro_uwb_session_set_time_offset(struct aliro_uwb_session *session, int64_t time_offset)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:377`

@brief Store the time offset used to synchronize clocks between reader and device.
@param session Session to update.
@param time_offset Time offset in microseconds.
@return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session is NULL.

### `enum aliro_uwb_err aliro_uwb_session_message_handle(struct aliro_uwb_session *session, struct aliro_uwb_message *message)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:395`

@brief Validate and dispatch an incoming Aliro UWB message to the appropriate protocol handler.
@param session Session that received the message.
@param message Message to validate and process.
@return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session or message is
NULL, ALIRO_UWB_ERR_MSG_MALFORMED if the message is shorter than the header or the payload length
does not match, ALIRO_UWB_ERR_MESSAGE_UNSUPPORTED for an unrecognized protocol.

### `enum aliro_uwb_err aliro_uwb_session_suspend(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:437`

@brief Suspend an active ranging session by sending a suspend request.
@param session Session to suspend.
@return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session is NULL,
ALIRO_UWB_ERR_INVALID_STATE if there is no active CCC session or the session is not in the
RANGING state, ALIRO_UWB_ERR_INTERNAL if the suspend request could not be built.

### `enum aliro_uwb_err aliro_uwb_session_forced_suspend(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:469`

@brief Forcibly stop the active CCC session, transitioning it to SUSPENDED without a
request/response exchange.
@param session Session to force-suspend.
@return ALIRO_UWB_ERR_NONE on success, ALIRO_UWB_ERR_INVALID_PARAMETER if session is NULL,
ALIRO_UWB_ERR_INVALID_STATE if no CCC session is active, otherwise the error translated from
cherry_ccc_session_stop.

### `enum aliro_uwb_err aliro_uwb_session_resume(struct aliro_uwb_session *session)`
`modules/woz_uwb/src/aliro/aliro_uwb_session.c:491`

@brief Resume a suspended ranging session by building and transmitting a resume request.
ALIRO_UWB_ERR_INVALID_STATE if there is no active CCC session or the session is not in the
SUSPENDED state, ALIRO_UWB_ERR_INTERNAL if the resume request could not be built.
