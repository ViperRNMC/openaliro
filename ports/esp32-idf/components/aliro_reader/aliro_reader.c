/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_reader Phase 2.3: session + transaction layer over the aliro_ble
 * transport. Mirrors the reference reader's model (CreateSession on connect,
 * HandleSessionData per SDU, DestroySession on disconnect), with heavy
 * diagnostic logging for bench bring-up. The M1-M4 crypto handshake that would
 * derive the URSK and arm UWB ranging is Phase 3 — stubbed and clearly marked.
 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"

#include "aliro_ble.h"
#include "woz_uwb_facade.h"
#include "aliro_reader.h"

static const char *TAG = "aliro_reader";

/* PROVISIONAL advertised BLE-UWB protocol version(s). The real values are the
 * provisioned Matter attribute aliroSupportedBLEUWBProtocolVersions (id 133),
 * set in Phase 4; reconcile before an iPhone will negotiate. */
static const uint16_t k_proto_versions[] = { 0x0100u };

/* Per-session Aliro transaction state. The Phase-3 handshake advances
 * IN_PROGRESS -> ESTABLISHED; this scaffold only observes + logs. */
enum aliro_txn_state {
	TXN_IDLE = 0,
	TXN_IN_PROGRESS, /* first message seen; handshake underway */
	TXN_ESTABLISHED, /* URSK derived; UWB ranging armed (Phase 3) */
	TXN_FAILED,
};

static const char *txn_state_str(enum aliro_txn_state s)
{
	switch (s) {
	case TXN_IDLE:        return "IDLE";
	case TXN_IN_PROGRESS: return "IN_PROGRESS";
	case TXN_ESTABLISHED: return "ESTABLISHED";
	case TXN_FAILED:      return "FAILED";
	default:              return "?";
	}
}

#define ALIRO_MAX_SESSIONS 2

static struct aliro_session {
	bool active;
	uint16_t conn_handle;
	enum aliro_txn_state state;
	uint32_t msgs_rx;
} s_sessions[ALIRO_MAX_SESSIONS];

static struct aliro_session *session_find(uint16_t conn_handle)
{
	for (int i = 0; i < ALIRO_MAX_SESSIONS; i++) {
		if (s_sessions[i].active && s_sessions[i].conn_handle == conn_handle) {
			return &s_sessions[i];
		}
	}
	return NULL;
}

static struct aliro_session *session_alloc(uint16_t conn_handle)
{
	for (int i = 0; i < ALIRO_MAX_SESSIONS; i++) {
		if (!s_sessions[i].active) {
			s_sessions[i].active = true;
			s_sessions[i].conn_handle = conn_handle;
			s_sessions[i].state = TXN_IDLE;
			s_sessions[i].msgs_rx = 0;
			return &s_sessions[i];
		}
	}
	return NULL;
}

/*
 * Phase 3 landing zone: once the handshake has derived the 32-byte URSK and the
 * negotiated ranging parameters, populate cfg and arm the UWB responder:
 *     woz_uwb_bind_ursk(ursk, 32);
 *     woz_uwb_start_aliro(&cfg);
 * Not reached yet — the URSK comes from the unimplemented M1-M4 crypto.
 */
static void arm_uwb_from_credential(struct aliro_session *sess)
{
	ESP_LOGW(TAG, "[conn %u] would arm UWB from derived credential here "
		      "(Phase 3: URSK derivation not implemented)", sess->conn_handle);
	/* struct woz_uwb_aliro_cfg cfg = { ... derived ... };
	 * woz_uwb_start_aliro(&cfg); */
}

/* Consume one inbound Aliro transaction SDU. Phase-3 crypto stubbed. */
static void transaction_feed(struct aliro_session *sess, const uint8_t *data, uint16_t len)
{
	sess->msgs_rx++;
	uint8_t opcode = (len > 0) ? data[0] : 0xffu;

	ESP_LOGI(TAG, "[conn %u] Aliro msg #%u: %u bytes, opcode=0x%02x, state=%s",
		 sess->conn_handle, (unsigned)sess->msgs_rx, (unsigned)len, opcode,
		 txn_state_str(sess->state));
	ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);

	if (sess->state == TXN_IDLE) {
		sess->state = TXN_IN_PROGRESS;
	}

	/*
	 * TODO(Phase 3): parse the M1-M4 envelope, run the Aliro cryptographic
	 * handshake (ephemeral ECDH + KDF -> shared secret -> URSK), build the
	 * response, and reply via aliro_ble_send(sess->conn_handle, ...). On
	 * success set TXN_ESTABLISHED and call arm_uwb_from_credential(). The
	 * crypto is unimplemented, so we only observe + log the messages.
	 */
	ESP_LOGW(TAG, "[conn %u] M1-M4 handshake not implemented (Phase 3) — "
		      "message observed, no response sent", sess->conn_handle);
	(void)&arm_uwb_from_credential; /* Phase-3 hook; referenced to keep it live */
}

/* ---- aliro_ble transport callbacks ---- */

static void on_connected(uint16_t conn_handle)
{
	struct aliro_session *s = session_alloc(conn_handle);
	if (s == NULL) {
		ESP_LOGE(TAG, "[conn %u] no free session slot", conn_handle);
		return;
	}
	ESP_LOGI(TAG, "[conn %u] Aliro session created", conn_handle);
}

static void on_disconnected(uint16_t conn_handle)
{
	struct aliro_session *s = session_find(conn_handle);
	if (s != NULL) {
		ESP_LOGI(TAG, "[conn %u] Aliro session destroyed (%u msgs, final state=%s)",
			 conn_handle, (unsigned)s->msgs_rx, txn_state_str(s->state));
		s->active = false;
	}
}

static void on_data(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
	struct aliro_session *s = session_find(conn_handle);
	if (s == NULL) {
		ESP_LOGW(TAG, "[conn %u] data for unknown session (%u bytes)",
			 conn_handle, (unsigned)len);
		return;
	}
	transaction_feed(s, data, len);
}

int aliro_reader_start(void)
{
	const struct aliro_ble_config cfg = {
		.proto_versions = k_proto_versions,
		.proto_versions_count = sizeof(k_proto_versions) / sizeof(k_proto_versions[0]),
		.features = {
			.timesync_procedure_0 = true,
			.timesync_procedure_1 = false,
			.le_coded_phy = false,
		},
		.cb = {
			.on_data = on_data,
			.on_connected = on_connected,
			.on_disconnected = on_disconnected,
		},
	};

	int rc = aliro_ble_start(&cfg);
	ESP_LOGI(TAG, "aliro_reader_start: transport %s (SPSM 0x%04x)",
		 rc == 0 ? "up" : "FAILED", aliro_ble_spsm());
	return rc;
}
