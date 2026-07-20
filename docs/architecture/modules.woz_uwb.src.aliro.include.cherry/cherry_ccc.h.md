<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`

@file cherry_ccc.h — CCC/Aliro-session interface (seam the adapter drives).

**depends on** [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_common.h`](cherry_common.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_session.h`](cherry_session.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](../modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](../modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](../modules.woz_uwb.src.aliro/aliro_uwb_session.c.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](../modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md)

## API

### `struct cherry_ccc_capabilities`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:26`

@brief Device CCC/Aliro capability set advertised to the peer.

### `struct cherry_ccc_session_event_session_status`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:84`

@brief Payload of a SESSION_STATUS event, giving the new session state and the reason for the
change.

### `struct cherry_ccc_session_event_error`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:92`

@brief Payload of a SESSION_ERROR event, giving the error status that triggered it.

### `struct cherry_ccc_controller_session_report`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:100`

@brief Opaque controller-side CCC session report; instances cross the API boundary only by
pointer.

### `struct cherry_ccc_controlee_session_report`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:105`

@brief Opaque CCC controlee (lock) session report structure, not defined outside the cherry
library.

#### `struct cherry_ccc_session *session`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:113`

@brief Opaque CCC session the event pertains to, defined by the shim.

#### `struct cherry_ccc_session_event_session_status *status`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:118`

@brief Payload pointer for a SESSION_STATUS event.

#### `struct cherry_ccc_session_event_error *error`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:122`

@brief Payload pointer for a SESSION_ERROR event.

#### `struct cherry_ccc_controller_session_report *controller_report`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:127`

@brief Pointer to the controller-side session report payload; report payloads
cross the seam only by pointer on this lock.

#### `struct cherry_ccc_controlee_session_report *controlee_report`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:132`

@brief Pointer to the controlee session report payload, carrying a distance and
timestamp snapshot.

#### `struct cherry_common_diag_report *diagnostics`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:137`

@brief Pointer to the diagnostic report snapshot (ranging samples, signal
metrics) for this CCC event.

### `struct cherry *ctx, cherry_ccc_cb_t callback, void *user_data,`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:177`

@brief Cherry library context managing CCC session lifecycle and event dispatch.

### `struct cherry_ccc_aliro_session_config *config);`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:181`

@brief Negotiated Aliro ranging parameters, filled in-place across M1-M4.

### `struct cherry_session *cherry_ccc_session_to_base(struct cherry_ccc_session *session)`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:186`

@brief Return the base session for a CCC session (the base is the first member).

### `cherry_ccc_session_get_user_data`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:189`

Convenience: fetch the CCC session's user_data via its base.

### `void cherry_ccc_event_free(struct cherry_ccc_event *event)`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:205`

@brief Release a CCC event and any heap payload it owns.
@param event Event to free.

### `cherry_ccc_session_destroy`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:208`

Destroy a CCC session (delegates to the base).

### `cherry_ccc_session_start`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:219`

Start a CCC session (delegates to the base).

### `cherry_ccc_session_stop`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:231`

Stop a CCC session (delegates to the base).

### `cherry_ccc_session_set_antennas`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:243`

Select round-1 antennas for a CCC session (delegates to the base).

### `cherry_ccc_session_set_diagnostics`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:280`

Enable/disable diagnostics for a CCC session (delegates to the base).

### `cherry_ccc_session_set_diagnostics(struct cherry_ccc_session *session, /** * @brief Diagnostic configuration for CCC session reporting (e.g., sampling interval, metrics to include). */ struct cherry_common_diag_cfg config)`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:284`

@brief Opaque CCC session being configured, defined by the shim.

### `struct cherry_common_diag_cfg config)`
`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h:288`

@brief Diagnostic configuration for CCC session reporting (e.g., sampling interval, metrics to include).
