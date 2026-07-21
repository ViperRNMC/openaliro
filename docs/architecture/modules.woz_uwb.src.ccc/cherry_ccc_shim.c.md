<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/ccc/cherry_ccc_shim.c`

@file cherry_ccc_shim.c — cherry_ccc_* seam (Aliro responder) implemented over the lock-native
FiRa MAC; maps each call onto woz_uwb_facade.

**depends on** [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](../modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](../modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_session.h`](../modules.woz_uwb.src.aliro.include.cherry/cherry_session.h.md), [`modules/woz_uwb/src/ccc/aliro_round_config.h`](aliro_round_config.h.md), [`modules/woz_uwb/src/facade/woz_alloc.h`](../modules.woz_uwb.src.facade/woz_alloc.h.md), [`modules/woz_uwb/src/facade/woz_util.h`](../modules.woz_uwb.src.facade/woz_util.h.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](../modules.woz_uwb.src.facade/woz_uwb_facade.h.md)

## API

### `struct cherry`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:27`

@brief Opaque Cherry context holder.
@param core_cb Core callback (never invoked: no UCI).
@param user_data Client data from cherry_create().

### `struct cherry_session`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:37`

@brief Base session object; first member of cherry_ccc_session for up-casting.
@param cb CCC notification callback from create.
@param user_data Client data (the aliro_uwb_session).

### `struct cherry_ccc_session`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:50`

@brief CCC ranging session bound to FiRa MAC.
@param base MUST be first member for up-casting.
@param config Borrowed pointer to adapter's negotiated params, valid until destroy.
@param ursk Provisioned-STS root key (16 bytes).
@param have_ursk True if URSK has been stashed via set_ursk.
@param state Last emitted session state (INIT, IDLE, ACTIVE, or DEINIT).

### `static inline struct cherry_ccc_session *to_ccc(struct cherry_session *base)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:60`

@brief Up-cast a base session pointer (base is the first member).

**called by** `cherry_session_destroy`, `cherry_session_start`, `cherry_session_stop`

### `static void emit_status(struct cherry_ccc_session *s, enum cherry_ccc_session_state st)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:70`

@brief Allocate and dispatch a SESSION_STATUS event to the CCC callback.
@param s CCC session.
@param st State to report (INIT, IDLE, ACTIVE, or DEINIT).

**called by** `cherry_session_destroy`, `cherry_session_start`, `cherry_session_stop`

### `static void emit_error(struct cherry_ccc_session *s, enum cherry_err err)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:95`

@brief Allocate + dispatch a SESSION_ERROR event to the CCC callback.

**called by** `cherry_session_start`

### `struct cherry *cherry_create(const char *device, cherry_core_cb_t core_cb, void *user_data)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:126`

@brief Allocate and initialize a Cherry context with the given core callback and user data.
@param device Device parameter (unused).
@param core_cb Core callback (never invoked).
@param user_data User data to store in the context.
@return Cherry context pointer, or null if allocation fails.

### `void cherry_destroy_sync(struct cherry *ctx)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:142`

@brief Deallocate a Cherry context; null input is safely ignored.

### `cherry_ccc_session_create_aliro_responder`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:157`

@brief Allocate and initialize an Aliro responder CCC session.
@param ctx Cherry context (unused).
@param callback CCC notification callback.
@param user_data User context to pass to callback.
@param config Session configuration (channel, session ID, STS index, timing, slot geometry).
@return Session pointer, or null if callback is null, config is null, or allocation fails.

### `struct cherry_session *cherry_ccc_session_to_base(struct cherry_ccc_session *session)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:185`

@brief Cast a CCC session pointer to its embedded base session structure.
@param session CCC session.
@return Base session pointer, or null if session is null.

### `void *cherry_session_get_user_data(struct cherry_session *session)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:195`

@brief Retrieve the user data pointer stored in the base session.
@param session Base session.
@return User data pointer, or null if session is null.

### `void cherry_session_destroy(struct cherry_session *session)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:204`

@brief Stop the UWB radio, emit a DEINIT status event, and deallocate the session.
@param session Base session; null or invalid input is safely ignored.

**calls** `emit_status`, `to_ccc`

### `enum cherry_err cherry_session_start(struct cherry_session *session)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:227`

@brief Start an Aliro UWB session by building a RangingConfiguration byte array from session
config, calling woz_uwb_start_aliro, and emitting IDLE then ACTIVE status events.
@param session Base session.
@return CHERRY_ERR_INVALID_PARAMETER if session or config is null; CHERRY_ERR_SESSION_CONFIG if
URSK is not set; CHERRY_ERR_SESSION_INIT if UWB start fails; otherwise CHERRY_ERR_NONE.

**calls** `emit_error`, `emit_status`, `to_ccc`

### `enum cherry_err cherry_session_stop(struct cherry_session *session)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:308`

@brief Stop the UWB radio and emit an IDLE status event.
@param session Base session.
@return CHERRY_ERR_INVALID_PARAMETER if session is null or invalid, otherwise CHERRY_ERR_NONE.

**calls** `emit_status`, `to_ccc`

### `enum cherry_err cherry_ccc_session_set_ursk(struct cherry_ccc_session *session, const uint8_t *ursk)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:328`

@brief Copy the URSK into the session and mark it as present.
@param session CCC session.
@param ursk 16-byte Unique Responder Session Key.
@return CHERRY_ERR_INVALID_PARAMETER if session or ursk is null, otherwise CHERRY_ERR_NONE.

### `enum cherry_err cherry_ccc_session_set_protocol_version(struct cherry_ccc_session *session, uint16_t selected_protocol_version)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:344`

@brief Validate that the session exists; selected protocol version is accepted but ignored.
@param session CCC session.
@param selected_protocol_version Protocol version (ignored).
@return CHERRY_ERR_INVALID_PARAMETER if session is null, otherwise CHERRY_ERR_NONE.

### `enum cherry_err cherry_ccc_session_set_sts_index(struct cherry_ccc_session *session, uint32_t sts_index)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:357`

@brief Store the STS index on the session config.
@param session CCC session.
@param sts_index STS index value.
@return CHERRY_ERR_INVALID_PARAMETER if session or its config is null, otherwise CHERRY_ERR_NONE.

### `enum cherry_err cherry_ccc_session_set_initiation_time(struct cherry_ccc_session *session, uint64_t initiation_time_us)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:373`

@brief Store the UWB initiation timestamp in microseconds on the session config.
@param session CCC session.
@param initiation_time_us Initiation timestamp in microseconds.
@return CHERRY_ERR_INVALID_PARAMETER if session or its config is null, otherwise CHERRY_ERR_NONE.

### `enum cherry_err cherry_ccc_session_set_round2_antennas(struct cherry_ccc_session *session, uint8_t tx_antenna_set, uint8_t rx_antenna_set)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:391`

@brief Validate that the session exists; TX and RX antenna set parameters are accepted but
ignored.
@param session CCC session.
@param tx_antenna_set TX antenna set (ignored).
@param rx_antenna_set RX antenna set (ignored).
@return CHERRY_ERR_INVALID_PARAMETER if session is null, otherwise CHERRY_ERR_NONE.

### `enum cherry_err cherry_session_set_antennas(struct cherry_session *session, uint8_t tx_antenna_set, uint8_t rx_antenna_set)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:408`

@brief Validate that the session exists; TX and RX antenna set parameters are accepted but
ignored.
@param session Base session.
@param tx_antenna_set TX antenna set (ignored).
@param rx_antenna_set RX antenna set (ignored).
@return CHERRY_ERR_INVALID_PARAMETER if session is null, otherwise CHERRY_ERR_NONE.

### `enum cherry_err cherry_session_set_diagnostics(struct cherry_session *session, struct cherry_common_diag_cfg config, bool controlee_only)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:420`

@brief Validate that the session exists; the diagnostics settings are accepted but ignored.
@return CHERRY_ERR_INVALID_PARAMETER if session is null, otherwise CHERRY_ERR_NONE.

### `void cherry_ccc_event_free(struct cherry_ccc_event *event)`
`modules/woz_uwb/src/ccc/cherry_ccc_shim.c:434`

@brief Free a CCC event and its payload; null input is safely ignored.
