// UWB ranging bring-up and lifecycle for the Aliro reader: initializes the reader's UWB
// adapter and Cherry CCC context once, then arms, feeds, and tears down per-connection ranging
// sessions driven by the M1-M4 setup exchanged over the peer's L2CAP channel.
// Maintains process-wide singletons for the Cherry context and adapter (set up once via
// aliro_ranging_init) and for the single active ranging session (the DW3000 supports only one
// session at a time), tracking its owning secure channel for send/receive framing.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_ranging — see aliro_ranging.h. Drives the engine reader adapter/session
 * for the post-auth M1-M4 ranging-setup exchange. Provenance: original glue over
 * the reverse-engineered engine adapter; call contract mirrors the reference
 * reader (integration/patches/custom_impl-uwb.patch).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "aliro_ble.h"
#include "aliro_crypto.h"
#include "woz_uwb_facade.h"

#include "cherry/cherry.h"
#include "cherry/cherry_ccc.h"
#include "aliro_uwb_adapter/aliro_uwb_adapter.h"
#include "aliro_uwb_adapter/aliro_uwb_session.h"

#include "aliro_ranging.h"

LOG_MODULE_REGISTER(aliro_ranging, CONFIG_WOZ_ALIRO_LOG_LEVEL);

#define ALIRO_VERSION        0x0100u
/* Upper bound on an inbound ranging SDU (mirrors the reference kMaxBleMessage). */
#define ALIRO_RANGING_MSG_MAX 256u

/* Reader-side selection preferences. BORROWED for the adapter's lifetime (the
 * adapter stores the pointer, not a copy), so this must have static storage. */
static struct aliro_uwb_adapter_reader_config s_reader_cfg = {
	.min_ran_multiplier = 1u,
	.preferred_hopping_configs = {
		.configs = { ALIRO_HOPPING_CONFIG_DISABLED,
			     ALIRO_HOPPING_CONFIG_CONTINUOUS_DEFAULT },
		.count = 2u,
	},
	.mac_mode = 0u,           /* 1 ranging round, offset 0, single antenna */
	.r1_antennas = { 0u, 0u },
	.r2_antennas = { 0u, 0u },
};

// Process-wide handle to the active Cherry CCC context, or NULL if ranging has not been set up.
static struct cherry *s_cherry;
// Process-wide handle to the active Aliro UWB adapter, or NULL if ranging has not been set up.
static struct aliro_uwb_adapter *s_adapter;

/* The single active ranging session (the DW3000 is single-session). Owned and
 * mutated only on the BLE-host task. s_sess is cleared when the engine frees the
 * session (a DEINIT status event) or when we tear it down. */
static struct aliro_uwb_session *s_sess;
static uint16_t s_sess_conn;
static bool s_sess_active;

/* The connection's BleSK ranging channel (owned by the reader session), used to
 * seal the engine's outbound SDUs. Borrowed for the session's lifetime. */
static struct aliro_secchan *s_sc_ble;

/* ---- engine callbacks (invoked synchronously on the BLE-host task) ---- */

/* Send an adapter-built message verbatim over the peer's L2CAP channel. The
 * bytes already carry the 4-byte Aliro header; hand them straight to the BLE
 * send. We own the message and MUST free it (even if we don't send). */
static void uwb_tx_cb(struct aliro_uwb_message *message,
		      struct aliro_uwb_session *session, void *user_data,
		      bool timeout)
{
	(void)session;
	(void)timeout;
	uint16_t conn = (uint16_t)(uintptr_t)user_data;

	if (message != NULL) {
		uint8_t wire[ALIRO_RANGING_MSG_MAX + ALIRO_GCM_TAG_LEN];
		size_t wl;

		/* The engine hands us a plaintext [proto][id][len][payload]; BleSK-seal it
		 * (§11.8.2, the 4-byte header as AAD) before it goes on the wire. */
		if (s_sc_ble != NULL &&
		    aliro_msg_seal(s_sc_ble, message->data, message->len, wire, sizeof(wire),
				   &wl) == 0) {
			int rc = aliro_ble_send(conn, wire, wl);

			LOG_INF("[conn %u] ranging TX proto=0x%02x id=0x%02x (%u B, rc=%d)",
				 conn, message->data[0], message->data[1], (unsigned)wl, rc);
		} else {
			LOG_ERR("[conn %u] ranging TX seal failed (%u B)", conn,
				 (unsigned)message->len);
		}
	}
	aliro_uwb_session_message_free(message);
}

/* Session notifications. On DEINIT the engine frees the session right after this
 * returns, so never touch event->session here; identify via user_data. Every
 * event must be freed. */
static void uwb_ev_cb(struct aliro_uwb_session_event *event, void *user_data)
{
	uint16_t conn = (uint16_t)(uintptr_t)user_data;

	if (event->type == ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_STATUS &&
	    event->data.status != NULL) {
		switch (event->data.status->session_state) {
		case CHERRY_CCC_SESSION_STATE_ACTIVE:
			LOG_INF("[conn %u] UWB ranging ACTIVE (negotiated "
				      "params live)", conn);
			break;
		case CHERRY_CCC_SESSION_STATE_IDLE:
			LOG_INF("[conn %u] UWB session IDLE", conn);
			break;
		case CHERRY_CCC_SESSION_STATE_DEINIT:
			LOG_INF("[conn %u] UWB session DEINIT (freed)", conn);
			if (s_sess_conn == conn) {
				s_sess = NULL;
				s_sess_active = false;
			}
			break;
		default:
			break;
		}
	} else if (event->type == ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_ERROR) {
		LOG_WRN("[conn %u] UWB session ERROR", conn);
	}
	aliro_uwb_session_event_free(event);
}

/* ---- public API ---- */

int aliro_ranging_init(void)
{
	if (s_adapter != NULL) {
		return 0; /* already up */
	}
	s_cherry = cherry_create("dw3000-fira", NULL, NULL);
	if (s_cherry == NULL) {
		LOG_ERR("cherry_create failed");
		return -1;
	}

	/* Advertised CCC capabilities. Deep-copied by create_reader, so these
	 * locals need not outlive the call. Values match the reference reader. */
	uint16_t proto[]       = { ALIRO_VERSION };
	uint16_t uwb_configs[] = { 0x0000u };
	uint8_t  pulse_combos[] = { 0x00u };
	// CCC capabilities advertised to the Aliro UWB adapter for this reader.
	struct cherry_ccc_capabilities ccc = {
		.slot_bitmask = 0xFFu,
		.sync_code_index_bitmask = 0x00000F00u, /* SYNC codes 9..12 */
		.hopping_config_bitmask = 0x1Au,        /* default + continuous + none */
		.channel_bitmask = 0x03u,               /* ch5 + ch9 */
		.protocol_versions = { .len = 1u, .items = proto },
		.uwb_configs = { .len = 1u, .items = uwb_configs },
		.pulse_shape_combos = { .len = 1u, .items = pulse_combos },
		.minimum_ran_multiplier = 1u,
		.qorvo_vendor_feature_1_supported = false,
	};
	// Device capabilities event reported by CCC, describing supported protocol versions, UWB configurations,
	// and pulse shape combinations.
	struct cherry_core_event_device_capabilities caps = {
		.status_err = CHERRY_ERR_NONE,
		.fira_capabilities = NULL,
		.ccc_capabilities = &ccc,
		.radar_capabilities = NULL,
	};

	s_adapter = aliro_uwb_adapter_create_reader(s_cherry, &caps, &s_reader_cfg);
	if (s_adapter == NULL) {
		LOG_ERR("aliro_uwb_adapter_create_reader failed");
		cherry_destroy_sync(s_cherry);
		s_cherry = NULL;
		return -1;
	}
	LOG_INF("UWB ranging adapter ready");

	/* Prove + initialise the DW3000 here, in the clean reader-startup task, so the
	 * heavy dwt_probe/dwt_initialise never runs from the BLE-host callback at M4.
	 * That callback path (aliro_ranging_feed -> engine -> woz_uwb_start_aliro) has a
	 * shallow stack and no prior bring-up, and dwt_probe failed there (-1). A one-shot
	 * start+stop with a throwaway URSK leaves the radio probed (uwb_min's g_radio_ready
	 * latches); the real session at M4 then re-uses it — ccc_prepoll_listen skips the
	 * probe and only re-applies the negotiated channel. Non-fatal: if the radio is
	 * absent, auth still runs and M4 will surface the failure. */
	static const uint8_t k_probe_ursk[ALIRO_URSK_LEN] = { 0 };
	// Aliro UWB Kconfig-equivalent probe configuration used to bring up the woz_uwb layer on this port.
	const struct woz_uwb_aliro_cfg probe_cfg = {
		.session_id = 0u,
		.channel = 9u,
		.sync_code_index = 9u,
		.slot_per_round = 1u,
		.ursk = k_probe_ursk,
	};
	if (woz_uwb_start_aliro(&probe_cfg) == 0) {
		woz_uwb_stop(); /* release RX; the radio stays probed */
		LOG_INF("DW3000 radio probed at init (M4 will reuse it)");
	} else {
		LOG_WRN("DW3000 probe at init failed; M4 handoff may not range");
	}
	return 0;
}

int aliro_ranging_start(uint16_t conn_handle, uint32_t session_id, const uint8_t *ursk,
			struct aliro_secchan *sc_ble)
{
	if (s_adapter == NULL || ursk == NULL || sc_ble == NULL) {
		LOG_WRN("[conn %u] ranging start: adapter/keys not ready", conn_handle);
		return -1;
	}
	if (s_sess_active) {
		LOG_WRN("[conn %u] ranging busy (active on conn %u); DW3000 is "
			      "single-session", conn_handle, s_sess_conn);
		return -1;
	}

	/* Release the demo responder so the radio is free for the negotiated
	 * session (the engine re-starts it with M1-M4 params at M4). */
	woz_uwb_stop();

	struct aliro_uwb_session *sess = aliro_uwb_session_create(
		s_adapter, session_id, uwb_ev_cb, uwb_tx_cb,
		(void *)(uintptr_t)conn_handle);

	if (sess == NULL) {
		LOG_ERR("[conn %u] session_create failed", conn_handle);
		return -1;
	}
	if (aliro_uwb_session_set_ursk(sess, ursk) != ALIRO_UWB_ERR_NONE) {
		LOG_ERR("[conn %u] set_ursk failed", conn_handle);
		aliro_uwb_session_destroy(sess);
		return -1;
	}
	aliro_uwb_session_set_protocol_version(sess, ALIRO_VERSION);

	s_sc_ble = sc_ble;
	s_sess = sess;
	s_sess_conn = conn_handle;
	s_sess_active = true;

	/* No eager M1: the engine emits it (via uwb_tx_cb, BleSK-sealed) when the
	 * device sends its Initiate-Ranging-Session (proto-2 id-1). */
	LOG_INF("[conn %u] ranging armed (session id 0x%08x); awaiting "
		      "Initiate-Ranging-Session", conn_handle, (unsigned)session_id);
	return 0;
}

int aliro_ranging_feed(uint16_t conn_handle, const uint8_t *data, size_t len)
{
	if (!s_sess_active || s_sess == NULL || s_sess_conn != conn_handle) {
		return -1;
	}
	if (len < 4u || len > ALIRO_RANGING_MSG_MAX) {
		LOG_WRN("[conn %u] ranging SDU size %u out of range", conn_handle,
			 (unsigned)len);
		return -1;
	}

	/* Stack-framed message (the engine copies/consumes it, does not retain). */
	uint8_t storage[sizeof(struct aliro_uwb_message) + ALIRO_RANGING_MSG_MAX]
		__attribute__((aligned(8)));
	struct aliro_uwb_message *msg = (struct aliro_uwb_message *)storage;

	msg->len = len;
	memcpy(msg->data, data, len);

	/* M4 makes the engine start the responder (cherry_ccc_shim ->
	 * woz_uwb_start_aliro) with the negotiated params. A hard error may DEINIT
	 * the session, which clears s_sess via uwb_ev_cb; don't touch it after. */
	enum aliro_uwb_err e = aliro_uwb_session_message_handle(s_sess, msg);

	if (e != ALIRO_UWB_ERR_NONE) {
		LOG_WRN("[conn %u] message_handle err %d", conn_handle, (int)e);
		return -1;
	}
	return 0;
}

void aliro_ranging_stop(uint16_t conn_handle)
{
	if (!s_sess_active || s_sess == NULL || s_sess_conn != conn_handle) {
		return;
	}
	struct aliro_uwb_session *sess = s_sess;

	/* Clear first so the DEINIT event this destroy emits is a no-op, and a
	 * direct free (no CCC session yet) leaves no dangling pointer. */
	s_sess = NULL;
	s_sess_active = false;
	s_sc_ble = NULL; /* borrowed from the reader session; drop before it is freed */
	LOG_INF("[conn %u] tearing down UWB ranging session", conn_handle);
	aliro_uwb_session_destroy(sess);
}
