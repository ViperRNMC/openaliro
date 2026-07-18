/** @file cherry_ccc_shim.c — cherry_ccc_* seam (Aliro responder) implemented over the lock-native
 * FiRa MAC; maps each call onto woz_uwb_facade. */

#include <cherry/cherry.h>
#include <cherry/cherry_ccc.h>
#include <cherry/cherry_session.h>

#include "woz_alloc.h"
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "woz_uwb_facade.h"
#include "aliro_round_config.h" /* ALIRO_NUM_RESPONDERS — EXPERIMENT-2RESP */

LOG_MODULE_REGISTER(woz_ccc_shim, LOG_LEVEL_INF);

/** URSK length (bytes) the Aliro provisioned-STS path expects. */
#define SHIM_URSK_LEN 32u

/** Opaque Cherry context: a holder for the (unused) core callback the adapter passes at create. */
struct cherry {
	cherry_core_cb_t core_cb; /**< Core callback (never invoked: no UCI). */
	void *user_data;          /**< Client data from cherry_create(). */
};

/** Base session object; first member of cherry_ccc_session so the base functions can up-cast. */
struct cherry_session {
	cherry_ccc_cb_t cb; /**< CCC notification callback (from create). */
	void *user_data;    /**< Client data (the aliro_uwb_session). */
};

/** CCC ranging session bound to our FiRa MAC. */
struct cherry_ccc_session {
	struct cherry_session base; /**< MUST be first (see up-cast below). */
	/** Borrowed pointer to the adapter's negotiated params, valid until destroy. */
	struct cherry_ccc_aliro_session_config *config;
	uint8_t ursk[SHIM_URSK_LEN];         /**< Provisioned-STS root key. */
	bool have_ursk;                      /**< URSK stashed via set_ursk. */
	enum cherry_ccc_session_state state; /**< Last emitted state. */
};

/** @brief Up-cast a base session pointer (base is the first member). */
static inline struct cherry_ccc_session *to_ccc(struct cherry_session *base)
{
	return (struct cherry_ccc_session *)base;
}

/** Allocate + dispatch a SESSION_STATUS event to the CCC callback (event and data heap-allocated).
 */
static void emit_status(struct cherry_ccc_session *s, enum cherry_ccc_session_state st)
{
	struct cherry_ccc_event *ev = qmalloc(sizeof(*ev));
	// Heap-allocated session status event data; contains a state enum to be reported via the
	// CCC callback.
	struct cherry_ccc_session_event_session_status *status = qmalloc(sizeof(*status));

	if (!ev || !status) {
		LOG_ERR("emit_status: OOM");
		qfree(ev);
		qfree(status);
		return;
	}

	status->session_state = st;
	status->reason_code = CHERRY_CCC_STATE_CHANGE_REASON_MGMT_CMD;
	ev->type = CHERRY_CCC_EVENT_TYPE_SESSION_STATUS;
	ev->session = s;
	ev->data.status = status;

	s->state = st;
	s->base.cb(ev, s->base.user_data);
}

/** @brief Allocate + dispatch a SESSION_ERROR event to the CCC callback. */
static void emit_error(struct cherry_ccc_session *s, enum cherry_err err)
{
	struct cherry_ccc_event *ev = qmalloc(sizeof(*ev));
	// Heap-allocated error event data; contains an error code to be reported via the CCC
	// callback.
	struct cherry_ccc_session_event_error *e = qmalloc(sizeof(*e));

	if (!ev || !e) {
		LOG_ERR("emit_error: OOM");
		qfree(ev);
		qfree(e);
		return;
	}

	e->status_err = err;
	ev->type = CHERRY_CCC_EVENT_TYPE_SESSION_ERROR;
	ev->session = s;
	ev->data.error = e;

	s->base.cb(ev, s->base.user_data);
}

/* ---- Cherry context lifecycle (no UCI; trivial holder) ------------------- */

// Allocate and initialize a Cherry context with the given core callback and user data; device
// parameter is unused; returns NULL if allocation fails.
struct cherry *cherry_create(const char *device, cherry_core_cb_t core_cb, void *user_data)
{
	struct cherry *ctx = qmalloc(sizeof(*ctx));

	ARG_UNUSED(device);
	if (!ctx) {
		return NULL;
	}
	ctx->core_cb = core_cb;
	ctx->user_data = user_data;
	return ctx;
}

// Deallocate a Cherry context; null input is safely ignored.
void cherry_destroy_sync(struct cherry *ctx)
{
	qfree(ctx);
}

/* ---- CCC session create + lifetime --------------------------------------- */

// Allocate and initialize an Aliro responder CCC session with the given callback, user context, and
// configuration; returns NULL if callback, config, or allocation fails.
struct cherry_ccc_session *
cherry_ccc_session_create_aliro_responder(struct cherry *ctx, cherry_ccc_cb_t callback,
					  void *user_data,
					  struct cherry_ccc_aliro_session_config *config)
{
	struct cherry_ccc_session *s;

	ARG_UNUSED(ctx);
	if (!callback || !config) {
		return NULL;
	}

	s = qcalloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	s->base.cb = callback;
	s->base.user_data = user_data;
	s->config = config;
	s->state = CHERRY_CCC_SESSION_STATE_INIT;
	return s;
}

struct cherry_session *
// Cast a CCC session pointer to its embedded base session structure; null propagates (the header
// wrappers rely on it).
cherry_ccc_session_to_base(struct cherry_ccc_session *session)
{
	return session ? &session->base : NULL;
}

// Retrieve the user data pointer stored in the base session; returns NULL if session is null.
void *cherry_session_get_user_data(struct cherry_session *session)
{
	return session ? session->user_data : NULL;
}

// Stop the UWB radio, emit a DEINIT status event, and deallocate the session; safe if session or
// its base pointer is null.
void cherry_session_destroy(struct cherry_session *session)
{
	struct cherry_ccc_session *s = to_ccc(session);

	if (!s) {
		return;
	}
	/* Tear the radio down, then tell the adapter the session is gone (it frees its own
	 * aliro_uwb_session on DEINIT). */
	woz_uwb_stop();
	emit_status(s, CHERRY_CCC_SESSION_STATE_DEINIT);
	qfree(s);
}

/* ---- Start / stop -------------------------------------------------------- */

// Start an Aliro UWB session by building a RangingConfiguration byte array from the session config,
// calling woz_uwb_start_aliro, and emitting IDLE then ACTIVE status events; returns
// CHERRY_ERR_INVALID_PARAMETER if session or config is null, CHERRY_ERR_SESSION_CONFIG if URSK is
// not set, or CHERRY_ERR_SESSION_INIT if the UWB start fails.
enum cherry_err cherry_session_start(struct cherry_session *session)
{
	struct cherry_ccc_session *s = to_ccc(session);
	const struct cherry_ccc_aliro_session_config *c;
	// Configuration struct for woz_uwb_start_aliro; holds session ID, channel, sync code,
	// timing, slot geometry, STS index, UWB time, URSK key, and the RangingConfiguration byte
	// array required to derive the CCC SaltedHash.
	struct woz_uwb_aliro_cfg fcfg;
	uint8_t rcfg[17];
	int rc;

	if (!s || !s->config) {
		return CHERRY_ERR_INVALID_PARAMETER;
	}
	if (!s->have_ursk) {
		LOG_ERR("start without URSK");
		return CHERRY_ERR_SESSION_CONFIG;
	}
	c = s->config;

	fcfg.session_id = c->session_id;
	fcfg.channel = c->channel;
	fcfg.sync_code_index = c->sync_code_index;
	fcfg.slot_duration_rstu = c->slot_duration;
	fcfg.block_duration_ms = c->ranging_duration_ms;
	fcfg.slot_per_round = c->slot_per_rr;
	fcfg.sts_index0 = c->sts_index;
	fcfg.uwb_time_us = c->uwb_time_us;
	fcfg.ursk = s->ursk;

	/* Build the RangingConfiguration (the CCC SaltedHash input the Wallet hashes): BE
	 * Protocol_Version + UWB_Config_Id + UWB_Session_Id + STS_Index0, then five 1-byte fields.
	 */
	rcfg[0] = 0x01u;
	rcfg[1] = 0x00u; /* Selected_Protocol_Version */
	rcfg[2] = (uint8_t)(c->uwb_config_id >> 8);
	rcfg[3] = (uint8_t)(c->uwb_config_id); /* Selected_UWB_Config_Id (BE) */
	rcfg[4] = (uint8_t)(c->session_id >> 24);
	rcfg[5] = (uint8_t)(c->session_id >> 16);
	rcfg[6] = (uint8_t)(c->session_id >> 8);
	rcfg[7] = (uint8_t)(c->session_id); /* UWB_Session_Id (BE) */
	rcfg[8] = (uint8_t)(c->sts_index >> 24);
	rcfg[9] = (uint8_t)(c->sts_index >> 16);
	rcfg[10] = (uint8_t)(c->sts_index >> 8);
	rcfg[11] = (uint8_t)(c->sts_index); /* STS_Index0 (BE) */
	rcfg[12] = (uint8_t)
		ALIRO_NUM_RESPONDERS; /* Number_Responder_Nodes — EXPERIMENT-2RESP; shared with M3
					 via aliro_round_config.h so the SaltedHash can't desync. */
	rcfg[13] = (uint8_t)(c->ranging_duration_ms / 96u); /* Session_RAN_Multiplier */
	rcfg[14] = c->slot_per_rr;                          /* Number_Slot_per_Round */
	rcfg[15] = (uint8_t)(c->slot_duration / 400u);      /* Number_Chaps_per_Slot */
	rcfg[16] = c->pulse_shape_combo;                    /* Selected_PulseShape_Combo */
	fcfg.ranging_config = rcfg;
	fcfg.rc_len = sizeof(rcfg);

	LOG_INF("Aliro start: sid=0x%08x ch=%u code=%u slot=%u blk=%ums spr=%u sts0=0x%08x",
		c->session_id, c->channel, c->sync_code_index, c->slot_duration,
		c->ranging_duration_ms, c->slot_per_rr, c->sts_index);
	LOG_INF("Aliro RangingConfig: proto=%02x%02x cfg=%02x%02x n=%u ran=%u spr=%u chap=%u "
		"ps=%02x",
		rcfg[0], rcfg[1], rcfg[2], rcfg[3], rcfg[12], rcfg[13], rcfg[14], rcfg[15],
		rcfg[16]);

	rc = woz_uwb_start_aliro(&fcfg);
	if (rc != 0) {
		LOG_ERR("woz_uwb_start_aliro rc=%d", rc);
		emit_error(s, CHERRY_ERR_SESSION_INIT);
		return CHERRY_ERR_SESSION_INIT;
	}

	/* Drive the add-on state machine INIT -> IDLE -> ACTIVE (== Ranging). */
	emit_status(s, CHERRY_CCC_SESSION_STATE_IDLE);
	emit_status(s, CHERRY_CCC_SESSION_STATE_ACTIVE);
	return CHERRY_ERR_NONE;
}

// Stop the UWB radio and emit an IDLE status event; returns CHERRY_ERR_INVALID_PARAMETER if session
// or its base pointer is null, otherwise CHERRY_ERR_NONE.
enum cherry_err cherry_session_stop(struct cherry_session *session)
{
	struct cherry_ccc_session *s = to_ccc(session);

	if (!s) {
		return CHERRY_ERR_INVALID_PARAMETER;
	}
	woz_uwb_stop();
	emit_status(s, CHERRY_CCC_SESSION_STATE_IDLE);
	return CHERRY_ERR_NONE;
}

/* ---- Fine session configuration (write-through to the borrowed config) --- */

// Copy the URSK (Unique Responder Session Key) into the session and mark it as present; returns
// CHERRY_ERR_INVALID_PARAMETER if session or ursk is null, otherwise CHERRY_ERR_NONE.
enum cherry_err cherry_ccc_session_set_ursk(struct cherry_ccc_session *session, const uint8_t *ursk)
{
	if (!session || !ursk) {
		return CHERRY_ERR_INVALID_PARAMETER;
	}
	memcpy(session->ursk, ursk, SHIM_URSK_LEN);
	session->have_ursk = true;
	return CHERRY_ERR_NONE;
}

enum cherry_err
// Validate that the session exists; the selected_protocol_version parameter is accepted but
// ignored; returns CHERRY_ERR_INVALID_PARAMETER if session is null, otherwise CHERRY_ERR_NONE.
cherry_ccc_session_set_protocol_version(struct cherry_ccc_session *session,
					uint16_t selected_protocol_version)
{
	ARG_UNUSED(selected_protocol_version);
	return session ? CHERRY_ERR_NONE : CHERRY_ERR_INVALID_PARAMETER;
}

enum cherry_err
// Store the STS index on the session config; returns CHERRY_ERR_INVALID_PARAMETER if session or its
// config is null, otherwise CHERRY_ERR_NONE.
cherry_ccc_session_set_sts_index(struct cherry_ccc_session *session, uint32_t sts_index)
{
	if (!session || !session->config) {
		return CHERRY_ERR_INVALID_PARAMETER;
	}
	session->config->sts_index = sts_index;
	return CHERRY_ERR_NONE;
}

enum cherry_err
// Store the UWB initiation timestamp in microseconds on the session config; returns
// CHERRY_ERR_INVALID_PARAMETER if session or its config is null, otherwise CHERRY_ERR_NONE.
cherry_ccc_session_set_initiation_time(struct cherry_ccc_session *session,
				       uint64_t initiation_time_us)
{
	if (!session || !session->config) {
		return CHERRY_ERR_INVALID_PARAMETER;
	}
	session->config->uwb_time_us = initiation_time_us;
	return CHERRY_ERR_NONE;
}

enum cherry_err
// Validate that the session exists; TX and RX antenna set parameters are accepted but ignored;
// returns CHERRY_ERR_INVALID_PARAMETER if session is null, otherwise CHERRY_ERR_NONE.
cherry_ccc_session_set_round2_antennas(struct cherry_ccc_session *session, uint8_t tx_antenna_set,
				       uint8_t rx_antenna_set)
{
	ARG_UNUSED(tx_antenna_set);
	ARG_UNUSED(rx_antenna_set);
	return session ? CHERRY_ERR_NONE : CHERRY_ERR_INVALID_PARAMETER;
}

// Validate that the session exists; TX and RX antenna set parameters are accepted but ignored;
// returns CHERRY_ERR_INVALID_PARAMETER if session is null, otherwise CHERRY_ERR_NONE.
enum cherry_err cherry_session_set_antennas(struct cherry_session *session, uint8_t tx_antenna_set,
					    uint8_t rx_antenna_set)
{
	ARG_UNUSED(tx_antenna_set);
	ARG_UNUSED(rx_antenna_set);
	return session ? CHERRY_ERR_NONE : CHERRY_ERR_INVALID_PARAMETER;
}

// Validate that the session exists; diagnostics config and controlee_only parameters are accepted
// but ignored; returns CHERRY_ERR_INVALID_PARAMETER if session is null, otherwise CHERRY_ERR_NONE.
enum cherry_err
cherry_session_set_diagnostics(struct cherry_session *session,
			       // Configuration structure for common CCC diagnostics reporting;
			       // passed to session_set_diagnostics but ignored in this shim.
			       struct cherry_common_diag_cfg config, bool controlee_only)
{
	ARG_UNUSED(config);
	ARG_UNUSED(controlee_only);
	return session ? CHERRY_ERR_NONE : CHERRY_ERR_INVALID_PARAMETER;
}

/* ---- Event teardown ------------------------------------------------------ */

// Free a CCC event handle.
void cherry_ccc_event_free(struct cherry_ccc_event *event)
{
	if (!event) {
		return;
	}
	switch (event->type) {
	case CHERRY_CCC_EVENT_TYPE_SESSION_STATUS:
		qfree(event->data.status);
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_ERROR:
		qfree(event->data.error);
		break;
	default:
		/* We never emit the report/diagnostic variants. */
		break;
	}
	qfree(event);
}
