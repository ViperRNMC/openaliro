/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_reader — the Aliro reader credential-auth transaction (Phase 3.2/3.3) on
 * top of the aliro_ble L2CAP transport. Drives AUTH0 -> AUTH1 -> EXCHANGE, runs
 * the ECDH + key schedule (aliro_crypto) to derive the URSK, then hands the URSK
 * to aliro_ranging, which negotiates the ranging parameters with the peer
 * (M1-M4) and starts the UWB responder. Wire codec = aliro_apdu; crypto =
 * aliro_crypto; ranging setup = aliro_ranging.
 *
 * Heavy diagnostic logging by design: this path can only complete end-to-end
 * once the reader is provisioned and a real credential is present. The reader
 * identity + credential trust store come from the provisioning seam (aliro_prov,
 * Phase 3.4): NVS if present, else a clearly-marked dev identity so the
 * transaction is drivable at bench before Phase-4 Matter provisioning lands.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "aliro_ble.h"
#include "aliro_apdu.h"
#include "aliro_crypto.h"
#include "aliro_prim.h"
#include "aliro_prov.h"
#include "aliro_ranging.h"
#include "aliro_reader.h"

static const char *TAG = "aliro_reader";

/* PROVISIONAL advertised BLE-UWB protocol version. Real value is the provisioned
 * Matter attribute (Phase 4); 0x0100 is the baseline the arbiter treats specially. */
static const uint16_t k_proto_versions[] = {0x0100u};
#define ALIRO_VERSION 0x0100u

/* Reader identity + credential trust store, loaded once at start from the
 * provisioning seam (NVS, else the clearly-marked dev identity). */
static struct aliro_reader_identity s_id;
static struct aliro_trust_store s_trust;
static bool s_loaded;

/* Most-recently-presented credential public key (the one the device signature
 * verified against). Captured for the `aliro-trust` bench command. */
static uint8_t s_last_cred_pub[ALIRO_CRED_PUB_LEN];
static bool s_have_last_cred;

/* Guards s_trust + s_last_cred_pub/s_have_last_cred, which the BLE-host task
 * (on_auth1_response) and the REPL task (the aliro-prov/aliro-trust commands)
 * both touch. s_id/s_loaded are set once at boot before the REPL starts, so they
 * need no lock. Created in load_provisioning() (runs single-threaded at boot). */
static SemaphoreHandle_t s_prov_lock;

enum txn_phase {
	PH_IDLE = 0,    /* connected; awaiting the peer's first message */
	PH_SENT_AUTH0,  /* AUTH0 sent; awaiting AUTH0Response */
	PH_SENT_AUTH1,  /* AUTH1 sent; awaiting AUTH1Response */
	PH_ESTABLISHED, /* URSK derived; ranging setup (M1-M4) driven by aliro_ranging */
	PH_FAILED,
};

static const char *phase_str(enum txn_phase p)
{
	switch (p) {
	case PH_IDLE:
		return "IDLE";
	case PH_SENT_AUTH0:
		return "SENT_AUTH0";
	case PH_SENT_AUTH1:
		return "SENT_AUTH1";
	case PH_ESTABLISHED:
		return "ESTABLISHED";
	case PH_FAILED:
		return "FAILED";
	default:
		return "?";
	}
}

#define ALIRO_MAX_SESSIONS 2

static struct aliro_session {
	bool active;
	uint16_t conn_handle;
	enum txn_phase phase;
	uint32_t msgs_rx;

	uint8_t reader_eph_priv[ALIRO_P256_SCALAR];
	uint8_t reader_eph_pub[ALIRO_P256_POINT];
	uint8_t txid[ALIRO_TXID_LEN];
	uint8_t device_eph_pub[ALIRO_P256_POINT];
	uint8_t z[32];
	struct aliro_secchan sc;
	uint8_t ursk[ALIRO_URSK_LEN];
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
			memset(&s_sessions[i], 0, sizeof(s_sessions[i]));
			s_sessions[i].active = true;
			s_sessions[i].conn_handle = conn_handle;
			s_sessions[i].phase = PH_IDLE;
			return &s_sessions[i];
		}
	}
	return NULL;
}

static void load_provisioning(void)
{
	if (s_prov_lock == NULL) {
		s_prov_lock = xSemaphoreCreateMutex();
	}
	if (s_loaded) {
		return;
	}
	int rc = aliro_prov_load(&s_id, &s_trust);

	if (s_id.is_dev) {
		ESP_LOGW(TAG,
			 "using DEV reader identity (Phase 4 supplies the real "
			 "one); %u trust anchor(s)",
			 s_trust.count);
	} else {
		ESP_LOGI(TAG, "provisioned reader identity loaded; %u trust anchor(s)",
			 s_trust.count);
	}
	ESP_LOGI(TAG, "prov source: %s", rc == 0 ? "NVS" : "dev default");
	s_loaded = true;
}

static int send_framed(uint16_t conn, uint8_t opcode, const uint8_t *payload, size_t len)
{
	uint8_t frame[600];
	size_t flen;

	if (aliro_ble_frame(0x00, opcode, payload, len, frame, sizeof(frame), &flen) != 0) {
		ESP_LOGE(TAG, "[conn %u] frame build failed (op 0x%02x len %u)", conn, opcode,
			 (unsigned)len);
		return -1;
	}
	int rc = aliro_ble_send(conn, frame, flen);

	ESP_LOGI(TAG, "[conn %u] TX op 0x%02x, %u payload bytes (send rc=%d)", conn, opcode,
		 (unsigned)len, rc);
	return rc;
}

/* Kick the reader-driven access protocol: ephemeral keys + txid -> AUTH0. */
static void start_auth(struct aliro_session *s)
{
	if (aliro_ec_p256_keygen(s->reader_eph_priv, s->reader_eph_pub) != 0 ||
	    aliro_random(s->txid, sizeof(s->txid)) != 0) {
		ESP_LOGE(TAG, "[conn %u] ephemeral keygen/txid failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}

	uint8_t apdu[160];
	size_t n;

	/* ExpeditedPhaseType/UserAuthenticationPolicy: standard path, no extra
	 * policy (enum values are provisioning/policy driven; 0x02/0x00 here). */
	if (aliro_apdu_build_auth0(0x02u, 0x00u, ALIRO_VERSION, s->reader_eph_pub, s->txid,
				   s_id.reader_id, apdu, sizeof(apdu), &n) != 0) {
		ESP_LOGE(TAG, "[conn %u] AUTH0 build failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	send_framed(s->conn_handle, ALIRO_OP_AUTH0, apdu, n);
	s->phase = PH_SENT_AUTH0;
}

static void on_auth0_response(struct aliro_session *s, const uint8_t *pl, size_t len)
{
	struct aliro_auth0_response r;

	if (aliro_apdu_parse_auth0_response(pl, len, &r) != 0) {
		ESP_LOGE(TAG, "[conn %u] AUTH0Response parse failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	memcpy(s->device_eph_pub, r.device_eph_pub, ALIRO_P256_POINT);

	uint8_t shared[ALIRO_SHARED_SECRET_LEN];

	if (aliro_ecdh_p256(s->reader_eph_priv, s->device_eph_pub, shared) != 0) {
		ESP_LOGE(TAG, "[conn %u] ECDH failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	aliro_crypto_derive_z(shared, s->txid, s->z);

	/* Sign the reader-usage transcript (device pubX, reader-eph pubX). */
	uint8_t td[160], sig[ALIRO_P256_SIG], apdu[128];
	size_t tn, n;

	if (aliro_apdu_build_authdata(ALIRO_AUTH_READER, s_id.reader_id, s->device_eph_pub + 1,
				      s->reader_eph_pub + 1, s->txid, td, sizeof(td), &tn) != 0 ||
	    aliro_ecdsa_p256_sign(s_id.sign_priv, td, tn, sig) != 0) {
		ESP_LOGE(TAG, "[conn %u] reader signature failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	if (aliro_apdu_build_auth1(0x01u, sig, apdu, sizeof(apdu), &n) != 0) {
		s->phase = PH_FAILED;
		return;
	}
	send_framed(s->conn_handle, ALIRO_OP_AUTH1, apdu, n);
	s->phase = PH_SENT_AUTH1;
}

static void on_auth1_response(struct aliro_session *s, const uint8_t *pl, size_t len)
{
	struct aliro_auth1_response r;

	if (aliro_apdu_parse_auth1_response(pl, len, &r) != 0) {
		ESP_LOGE(TAG, "[conn %u] AUTH1Response parse failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}

	/* Verify the device signature over the device-usage transcript. This proves
	 * possession of the presented key; whether that key is *trusted* is the
	 * separate trust-store check below. */
	const uint8_t *cred_pub = r.have_device_pub ? r.device_pub : s->device_eph_pub;
	uint8_t td[160];
	size_t tn;

	if (aliro_apdu_build_authdata(ALIRO_AUTH_DEVICE, s_id.reader_id, s->device_eph_pub + 1,
				      s->reader_eph_pub + 1, s->txid, td, sizeof(td), &tn) != 0) {
		s->phase = PH_FAILED;
		return;
	}
	if (aliro_ecdsa_p256_verify(cred_pub, td, tn, r.device_sig) != 0) {
		ESP_LOGW(TAG,
			 "[conn %u] device signature INVALID (expected until a "
			 "provisioned credential is present)",
			 s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	ESP_LOGI(TAG, "[conn %u] device signature OK", s->conn_handle);

	/* Remember the presented credential key for the `aliro-trust` bench command,
	 * and take the trust decision, under the lock the REPL commands share. The
	 * raw-key allowlist is the interim seam; real issuer-chain validation plugs
	 * in here (Phase 4). */
	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	memcpy(s_last_cred_pub, cred_pub, ALIRO_CRED_PUB_LEN);
	s_have_last_cred = true;
	int tv = aliro_prov_trust_check(&s_trust, cred_pub);
	xSemaphoreGive(s_prov_lock);

	if (tv == 0) {
		ESP_LOGI(TAG, "[conn %u] credential key TRUSTED", s->conn_handle);
	} else if (tv == 1 && s_id.is_dev) {
		ESP_LOGW(TAG,
			 "[conn %u] no trust anchors (DEV identity): accepting the "
			 "presented credential; run `aliro-trust` to enforce",
			 s->conn_handle);
	} else {
		ESP_LOGW(TAG, "[conn %u] credential key NOT trusted (%s); rejecting",
			 s->conn_handle, tv == 1 ? "no anchors provisioned" : "not in trust store");
		s->phase = PH_FAILED;
		return;
	}

	/* Establish the secure channel: HKDF key block -> session keys + URSK. */
	uint8_t salt[ALIRO_SALT_MAX], block[ALIRO_KEY_BLOCK_LEN];
	uint8_t enc[ALIRO_SESSION_KEY_LEN], dec[ALIRO_SESSION_KEY_LEN];
	size_t slen;

	if (aliro_salt_build(ALIRO_SALT_SESSION, s->txid, s->device_eph_pub + 1,
			     s->reader_eph_pub + 1, s_id.reader_id, ALIRO_VERSION, 0x02u, 0x00u,
			     NULL, salt, &slen) != 0 ||
	    aliro_crypto_derive_block(s->z, salt, slen, s->device_eph_pub + 1, block) != 0) {
		ESP_LOGE(TAG, "[conn %u] key-block derivation failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	aliro_crypto_split(block, 1, enc, dec, s->ursk);
	aliro_secchan_init(&s->sc, enc, dec);

	/* EXCHANGE: seal the URSK-ready trigger and frame it. */
	uint8_t ex[16], ct[16], tag[ALIRO_GCM_TAG_LEN], payload[32];
	size_t exn;

	if (aliro_apdu_build_exchange(0, 0, 1, ex, sizeof(ex), &exn) != 0 ||
	    aliro_secchan_seal(&s->sc, NULL, 0, ex, exn, ct, tag) != 0) {
		ESP_LOGE(TAG, "[conn %u] EXCHANGE seal failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	memcpy(payload, ct, exn);
	memcpy(payload + exn, tag, ALIRO_GCM_TAG_LEN);
	send_framed(s->conn_handle, ALIRO_OP_EXCHANGE, payload, exn + ALIRO_GCM_TAG_LEN);

	/* Auth done: hand the derived URSK to the ranging module, which emits M1 and
	 * negotiates the ranging parameters with the peer (M1-M4). */
	s->phase = PH_ESTABLISHED;
	ESP_LOGI(TAG, "[conn %u] URSK derived; starting ranging setup", s->conn_handle);
	ESP_LOG_BUFFER_HEXDUMP(TAG, s->ursk, ALIRO_URSK_LEN, ESP_LOG_INFO);
	if (aliro_ranging_start(s->conn_handle, s->ursk) != 0) {
		ESP_LOGW(TAG, "[conn %u] ranging setup did not start", s->conn_handle);
	}
}

/* Consume one inbound Aliro transaction SDU. */
static void transaction_feed(struct aliro_session *s, const uint8_t *data, uint16_t len)
{
	s->msgs_rx++;

	uint8_t type, opcode;
	const uint8_t *pl;
	size_t pl_len;

	if (aliro_ble_unframe(data, len, &type, &opcode, &pl, &pl_len) != 0) {
		ESP_LOGW(TAG, "[conn %u] msg #%u (%u B): not a valid envelope", s->conn_handle,
			 (unsigned)s->msgs_rx, (unsigned)len);
		ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);
		pl = data;
		pl_len = len;
		type = 0xff;
		opcode = 0xff;
	}
	ESP_LOGI(TAG, "[conn %u] msg #%u: type=0x%02x op=0x%02x, %u payload B, phase=%s",
		 s->conn_handle, (unsigned)s->msgs_rx, type, opcode, (unsigned)pl_len,
		 phase_str(s->phase));

	switch (s->phase) {
	case PH_IDLE:
		/* First peer message = SELECT / proprietary-info. Version negotiation
		 * rides the GATT characteristic; here we log it and drive AUTH0.
		 * (Bench-tunable: some peers expect the reader to send AUTH0 on
		 * channel-up rather than after this first message.) */
		ESP_LOGI(TAG, "[conn %u] peer opened; starting access protocol", s->conn_handle);
		start_auth(s);
		break;
	case PH_SENT_AUTH0:
		on_auth0_response(s, pl, pl_len);
		break;
	case PH_SENT_AUTH1:
		on_auth1_response(s, pl, pl_len);
		break;
	case PH_ESTABLISHED:
		/* Post-auth ranging-setup (M1-M4): route the raw SDU (its own 4-byte
		 * Aliro header included, not the unframed auth payload) to the engine. */
		aliro_ranging_feed(s->conn_handle, data, len);
		break;
	default:
		ESP_LOGW(TAG, "[conn %u] message in phase %s ignored", s->conn_handle,
			 phase_str(s->phase));
		break;
	}
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
		ESP_LOGI(TAG, "[conn %u] Aliro session destroyed (%u msgs, phase=%s)", conn_handle,
			 (unsigned)s->msgs_rx, phase_str(s->phase));
		s->active = false;
	}
	aliro_ranging_stop(conn_handle);
}

static void on_data(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
	struct aliro_session *s = session_find(conn_handle);

	if (s == NULL) {
		ESP_LOGW(TAG, "[conn %u] data for unknown session (%u bytes)", conn_handle,
			 (unsigned)len);
		return;
	}
	transaction_feed(s, data, len);
}

/* The reader's BLE transport config: advertised versions/features + the
 * transaction transport callbacks. Shared by the standalone + attached starts. */
static struct aliro_ble_config make_ble_cfg(void)
{
	const struct aliro_ble_config cfg = {
		.proto_versions = k_proto_versions,
		.proto_versions_count = sizeof(k_proto_versions) / sizeof(k_proto_versions[0]),
		.features =
			{
				.timesync_procedure_0 = true,
				.timesync_procedure_1 = false,
				.le_coded_phy = false,
			},
		.cb =
			{
				.on_data = on_data,
				.on_connected = on_connected,
				.on_disconnected = on_disconnected,
			},
	};
	return cfg;
}

/* crypto + provisioning load + UWB ranging setup, shared by both start paths. */
static int reader_engine_init(void)
{
	if (aliro_crypto_init() != 0) {
		ESP_LOGE(TAG, "crypto init failed");
		return -1;
	}
	load_provisioning();
	if (aliro_ranging_init() != 0) {
		ESP_LOGW(TAG, "UWB ranging adapter unavailable; auth will run but "
			      "ranging setup won't start");
	}
	return 0;
}

int aliro_reader_start(void)
{
	if (reader_engine_init() != 0) {
		return -1;
	}
	struct aliro_ble_config cfg = make_ble_cfg();
	int rc = aliro_ble_start(&cfg);

	ESP_LOGI(TAG, "aliro_reader_start: transport %s (SPSM 0x%04x)", rc == 0 ? "up" : "FAILED",
		 aliro_ble_spsm());
	return rc;
}

/* ---- attach mode: share a host another stack (e.g. Matter) owns ---------- */

const void *aliro_reader_ble_prepare(void)
{
	struct aliro_ble_config cfg = make_ble_cfg();

	if (aliro_ble_prepare(&cfg) != 0) {
		ESP_LOGE(TAG, "aliro_ble_prepare failed");
		return NULL;
	}
	return aliro_ble_service_def();
}

int aliro_reader_start_attached(void)
{
	if (reader_engine_init() != 0) {
		return -1;
	}

	/* Provisioned Aliro advertising params (BLE-UWB approach discovery): the phone
	 * resolves "its" reader by re-deriving the dynamic tag from the GroupResolvingKey.
	 * groupId = reader_id[0..7], subId = reader_id[16..17] (the reader identity is
	 * groupIdentifier(16) || groupSubIdentifier(16)). Only advertise the resolvable
	 * service data when a real GRK is present (Matter-provisioned). */
	bool have_grk = false;
	for (size_t i = 0; i < ALIRO_GRK_LEN; i++) {
		if (s_id.grk[i] != 0u) {
			have_grk = true;
			break;
		}
	}
	if (have_grk) {
		const uint8_t sub2[2] = { s_id.reader_id[16], s_id.reader_id[17] };

		aliro_ble_set_adv_params(&s_id.reader_id[0], sub2, s_id.grk, 0 /* tx power */);
	}

	int rc = aliro_ble_start_attached();

	ESP_LOGI(TAG, "aliro_reader_start_attached: %s (SPSM 0x%04x)", rc == 0 ? "up" : "FAILED",
		 aliro_ble_spsm());
	return rc;
}

void aliro_reader_refresh_adv(void)
{
	/* Matter provisioning (SetAliroReaderConfig) can land after the reader has
	 * already started advertising: Apple sends it as post-commissioning operational
	 * commands, whereas the reader starts on kCommissioningComplete. At start the
	 * identity was still the dev default (no GRK), so the reader advertised only the
	 * bare 0xFFF2 UUID and the phone cannot resolve it. Once the real GRK is in
	 * s_id (provision_identity ran just before), pull it into the advertisement. */
	bool have_grk = false;
	for (size_t i = 0; i < ALIRO_GRK_LEN; i++) {
		if (s_id.grk[i] != 0u) {
			have_grk = true;
			break;
		}
	}
	if (!have_grk) {
		return;
	}
	const uint8_t sub2[2] = { s_id.reader_id[16], s_id.reader_id[17] };

	aliro_ble_set_adv_params(&s_id.reader_id[0], sub2, s_id.grk, 0 /* tx power */);
	aliro_ble_readvertise();
	ESP_LOGI(TAG, "advertisement refreshed with provisioned GRK (approach-resolvable)");
}

/* ---- bench provisioning helpers (aliro-prov / aliro-trust) ------------- */

void aliro_reader_prov_print(void)
{
	load_provisioning();

	/* Snapshot the shared state under the lock, then print off the copy so the
	 * UART I/O never holds up the BLE task's trust check. */
	struct aliro_trust_store ts;
	uint8_t last[ALIRO_CRED_PUB_LEN];
	bool have;

	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	ts = s_trust;
	have = s_have_last_cred;
	memcpy(last, s_last_cred_pub, ALIRO_CRED_PUB_LEN);
	xSemaphoreGive(s_prov_lock);

	printf("identity  : %s\n", s_id.is_dev ? "DEV (bench)" : "provisioned");
	printf("reader_id : ");
	for (unsigned i = 0; i < ALIRO_READER_ID_LEN; i++) {
		printf("%02x", s_id.reader_id[i]);
	}
	printf("\ntrust     : %u/%u anchor(s)\n", ts.count, ALIRO_TRUST_MAX);
	for (unsigned i = 0; i < ts.count; i++) {
		printf("  [%u] ", i);
		for (unsigned j = 0; j < ALIRO_CRED_PUB_LEN; j++) {
			printf("%02x", ts.cred_pub[i][j]);
		}
		printf("\n");
	}
	printf("last cred : ");
	if (have) {
		for (unsigned i = 0; i < ALIRO_CRED_PUB_LEN; i++) {
			printf("%02x", last[i]);
		}
		printf("\n");
	} else {
		printf("(none presented yet)\n");
	}
}

int aliro_reader_trust_last(void)
{
	load_provisioning();

	/* Build the candidate store off a snapshot, persist it, then commit — so the
	 * NVS write happens outside the lock and a failed write changes nothing. */
	struct aliro_trust_store cand;
	uint8_t last[ALIRO_CRED_PUB_LEN];
	bool have;

	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	cand = s_trust;
	have = s_have_last_cred;
	memcpy(last, s_last_cred_pub, ALIRO_CRED_PUB_LEN);
	xSemaphoreGive(s_prov_lock);

	if (!have) {
		return 1; /* nothing presented yet */
	}
	int add = aliro_prov_trust_add(&cand, last);

	if (add == 1) {
		return 1; /* already trusted; nothing to persist */
	}
	if (add < 0) {
		return -1; /* store full */
	}
	if (aliro_prov_store(&s_id, &cand) != 0) {
		return -1; /* not committed; s_trust unchanged */
	}
	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	s_trust = cand;
	xSemaphoreGive(s_prov_lock);
	return 0;
}

/* ---- Matter provisioning bridge (Phase 4) ------------------------------ *
 * These run on the Matter task during commissioning, before the reader is
 * started (the reader starts only after the BLE handoff). They mutate the same
 * s_id/s_trust the reader loads, and persist through aliro_prov, so a snapshot-
 * then-store keeps NVS and the in-memory copy consistent under s_prov_lock. */

int aliro_reader_provision_identity(const uint8_t reader_id[ALIRO_READER_ID_LEN],
				    const uint8_t sign_priv[ALIRO_READER_PRIV_LEN],
				    const uint8_t grk[ALIRO_GRK_LEN])
{
	load_provisioning();

	struct aliro_reader_identity id;
	struct aliro_trust_store ts;

	memcpy(id.reader_id, reader_id, ALIRO_READER_ID_LEN);
	memcpy(id.sign_priv, sign_priv, ALIRO_READER_PRIV_LEN);
	memcpy(id.grk, grk, ALIRO_GRK_LEN);
	id.is_dev = false;

	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	ts = s_trust; /* keep any anchors already added */
	xSemaphoreGive(s_prov_lock);

	if (aliro_prov_store(&id, &ts) != 0) {
		return -1; /* not committed; s_id unchanged */
	}
	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	s_id = id;
	xSemaphoreGive(s_prov_lock);
	ESP_LOGI(TAG, "Matter-provisioned reader identity stored");
	return 0;
}

int aliro_reader_provision_add_trust(const uint8_t cred_pub[ALIRO_CRED_PUB_LEN])
{
	load_provisioning();

	struct aliro_reader_identity id;
	struct aliro_trust_store cand;

	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	id = s_id;
	cand = s_trust;
	xSemaphoreGive(s_prov_lock);

	int add = aliro_prov_trust_add(&cand, cred_pub);

	if (add == 1) {
		return 1; /* already trusted; nothing to persist */
	}
	if (add < 0) {
		return -1; /* store full or not a P-256 point */
	}
	if (aliro_prov_store(&id, &cand) != 0) {
		return -1; /* not committed; s_trust unchanged */
	}
	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	s_trust = cand;
	xSemaphoreGive(s_prov_lock);
	ESP_LOGI(TAG, "Matter-provisioned trust anchor stored (%u total)", cand.count);
	return 0;
}

int aliro_reader_provision_clear(void)
{
	load_provisioning();

	struct aliro_reader_identity id;
	struct aliro_trust_store ts;

	aliro_prov_dev_default(&id, &ts);
	if (aliro_prov_store(&id, &ts) != 0) {
		return -1;
	}
	xSemaphoreTake(s_prov_lock, portMAX_DELAY);
	s_id = id;
	s_trust = ts;
	xSemaphoreGive(s_prov_lock);
	ESP_LOGI(TAG, "reader provisioning cleared (reverted to dev identity)");
	return 0;
}
