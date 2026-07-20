// Aliro reader engine: drives the Access Protocol (AUTH0/AUTH1/EXCHANGE) handshake over BLE,
// manages reader identity and credential trust provisioning in NVS, and arms UWB ranging once
// a session is authenticated. Maintains a fixed-size table of per-connection sessions tracking
// transaction phase and secure-channel state, and exposes start/attach entry points for both
// standalone and Matter-attached BLE transports, plus provisioning and diagnostic APIs used by
// Matter commissioning and the bench console.
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

#include "woz_port.h"
#include "woz_log.h"

#include "aliro_ble.h"
#include "aliro_apdu.h"
#include "aliro_crypto.h"
#include "aliro_prim.h"
#include "aliro_prov.h"
#include "aliro_ranging.h"
#include "aliro_reader.h"

LOG_MODULE_REGISTER(aliro_reader, CONFIG_WOZ_ALIRO_LOG_LEVEL);

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

/* The credential key of the most recent session that passed the trust check, as
 * opposed to s_last_cred_pub which is every key presented. Attribution only: the
 * Matter LockOperation event names the user this key belongs to. */
static uint8_t s_auth_cred_pub[ALIRO_CRED_PUB_LEN];
static bool s_have_auth_cred;

/* Reader group key X = the X coordinate of pub(sign_priv). This is salt field 1
 * (reader_group_identifier_key.x) in the §8.3.1.13 key schedule; both sides must
 * agree on it, so it is derived from the provisioned signingKey (its public
 * counterpart = the verificationKey the credential was provisioned with).
 * Recomputed whenever s_id changes. */
static uint8_t s_reader_group_x[ALIRO_EC_PUBX_LEN];
static bool s_have_group_x;

/* Fallback SELECT-response 0xA5 proprietary-info TLV for a CSA-app, protocol
 * v1.0-only device (Table 10-2): a5 08 [80 02 0000] [5c 02 0100]. Used only when
 * the phone's op-0x05 message carried no 0xA5 TLV (a live device sends its own). */
static const uint8_t k_a5_csa_v1[] = {
	0xa5, 0x08, 0x80, 0x02, 0x00, 0x00, 0x5c, 0x02, 0x01, 0x00,
};

/* Recover the reader group key X from the provisioned signingKey. Call after any
 * mutation of s_id. Leaves s_have_group_x=false (and logs) on failure. */
static void compute_reader_group_x(void)
{
	uint8_t pub[ALIRO_P256_POINT];

	if (aliro_ec_p256_pub_from_priv(s_id.sign_priv, pub) == 0) {
		memcpy(s_reader_group_x, pub + 1, ALIRO_EC_PUBX_LEN);
		s_have_group_x = true;
	} else {
		s_have_group_x = false;
		LOG_ERR("reader group key derivation failed; salt field 1 unavailable");
	}
}

/* Guards s_trust + s_last_cred_pub/s_have_last_cred + s_auth_cred_pub/
 * s_have_auth_cred, which the BLE-host task (on_auth1_response), the REPL task
 * (the aliro-prov/aliro-trust commands) and the reader task (the attribution
 * lookup) all touch. s_id/s_loaded are set once at boot before the REPL starts,
 * so they need no lock. Created in load_provisioning() (single-threaded boot). */
static woz_mutex_t s_prov_lock;
static bool s_prov_lock_ready;

enum txn_phase {
	PH_IDLE = 0,      /* connected; awaiting the peer's first message */
	PH_SENT_AUTH0,    /* AUTH0 sent; awaiting AUTH0Response */
	PH_SENT_AUTH1,    /* AUTH1 sent; awaiting AUTH1Response */
	PH_SENT_EXCHANGE, /* EXCHANGE sent; awaiting its response, then AP-Completed */
	PH_ESTABLISHED,   /* AP-Completed sent; ranging setup (M1-M4) driven by aliro_ranging */
	PH_FAILED,
};

// Returns a human-readable name for a transaction phase enum value, or "?" for an unrecognized
// value.
static const char *phase_str(enum txn_phase p)
{
	switch (p) {
	case PH_IDLE:
		return "IDLE";
	case PH_SENT_AUTH0:
		return "SENT_AUTH0";
	case PH_SENT_AUTH1:
		return "SENT_AUTH1";
	case PH_SENT_EXCHANGE:
		return "SENT_EXCHANGE";
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
	struct aliro_secchan sc;     /* AP secure channel (ExpeditedSK) */
	struct aliro_secchan sc_ble; /* ranging channel (BleSKReader/Device), §11.8 */
	uint8_t ursk[ALIRO_URSK_LEN];

	/* The phone's 0xA5 proprietary-info TLV (tag+len+value), captured from its
	 * op-0x05 Initiate-Access-Protocol message; the trailing field of the
	 * session-key salt. a5_len==0 means none seen (use the CSA v1.0 default). */
	uint8_t a5_tlv[64];
	size_t a5_len;
} s_sessions[ALIRO_MAX_SESSIONS];

// Finds the active session matching the given BLE connection handle.
// Returns a pointer to the matching session, or NULL if no active session has that conn_handle.
static struct aliro_session *session_find(uint16_t conn_handle)
{
	for (int i = 0; i < ALIRO_MAX_SESSIONS; i++) {
		if (s_sessions[i].active && s_sessions[i].conn_handle == conn_handle) {
			return &s_sessions[i];
		}
	}
	return NULL;
}

// Allocates and returns the first inactive slot in the fixed-size session table for a new
// connection, initializing it to phase PH_IDLE. Returns NULL if all ALIRO_MAX_SESSIONS slots are
// already active.
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

// Loads the reader's provisioning state (identity, trust anchors) from NVS into the module-level
// s_id/s_trust, lazily creating the provisioning mutex on first call. Idempotent: does nothing on
// subsequent calls once s_loaded is set. Logs whether a dev-default or real identity was loaded and
// its source (NVS vs. dev default), then recomputes the reader group X coordinate.
static void load_provisioning(void)
{
	if (!s_prov_lock_ready) {
		woz_mutex_init(&s_prov_lock);
		s_prov_lock_ready = true;
	}
	if (s_loaded) {
		return;
	}
	int rc = aliro_prov_load(&s_id, &s_trust);

	if (s_id.is_dev) {
		LOG_WRN("using DEV reader identity (Phase 4 supplies the real "
			"one); %u trust anchor(s)",
			s_trust.count);
	} else {
		LOG_INF("provisioned reader identity loaded; %u trust anchor(s)", s_trust.count);
	}
	LOG_INF("prov source: %s", rc == 0 ? "NVS" : "dev default");
	compute_reader_group_x();
	s_loaded = true;
}

/* Frame + send an Access-Protocol command: wrap the command TLV in an ISO7816
 * APDU (ins selects AUTH0/AUTH1/EXCHANGE), then a BLE Access frame
 * (type=ACCESS, opcode=AP_OP_COMMAND). The command byte lives in the APDU INS,
 * NOT the BLE opcode — the phone rejects a raw TLV under opcode=INS. */
static int send_ap_command(uint16_t conn, uint8_t ins, const uint8_t *tlv, size_t len)
{
	/* One buffer [type][op][len_be16][APDU]. This runs on the small nimble_host
	 * callback stack, so build the APDU past a 4-byte header and fill the header in
	 * place rather than staging a second frame buffer. AUTH0/AUTH1/EXCHANGE APDUs
	 * are all well under 256 B. */
	uint8_t frame[ALIRO_ENVELOPE_HDR + 256];
	size_t alen;

	if (aliro_apdu_wrap(ins, tlv, len, frame + ALIRO_ENVELOPE_HDR,
			    sizeof(frame) - ALIRO_ENVELOPE_HDR, &alen) != 0) {
		LOG_ERR("[conn %u] APDU wrap failed (ins 0x%02x len %u)", conn, ins, (unsigned)len);
		return -1;
	}
	frame[0] = ALIRO_PROTO_ACCESS;
	frame[1] = ALIRO_AP_OP_COMMAND;
	frame[2] = (uint8_t)(alen >> 8);
	frame[3] = (uint8_t)(alen & 0xffu);

	int rc = aliro_ble_send(conn, frame, ALIRO_ENVELOPE_HDR + alen);

	LOG_INF("[conn %u] TX ins 0x%02x, %u APDU bytes (send rc=%d)", conn, ins, (unsigned)alen,
		rc);
	return rc;
}

/* Kick the reader-driven access protocol: ephemeral keys + txid -> AUTH0. */
static void start_auth(struct aliro_session *s)
{
	if (aliro_ec_p256_keygen(s->reader_eph_priv, s->reader_eph_pub) != 0 ||
	    aliro_random(s->txid, sizeof(s->txid)) != 0) {
		LOG_ERR("[conn %u] ephemeral keygen/txid failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}

	uint8_t apdu[160];
	size_t n;

	/* Standard-path AUTH0: ExpeditedPhaseType 0 (encoded as an empty 41 00) and
	 * UserAuthenticationPolicy 0x01 — values confirmed from the reference
	 * AUTH0Command::Serialize. These same two values feed the session-key salt below. */
	if (aliro_apdu_build_auth0(0x00u, 0x01u, ALIRO_VERSION, s->reader_eph_pub, s->txid,
				   s_id.reader_id, apdu, sizeof(apdu), &n) != 0) {
		LOG_ERR("[conn %u] AUTH0 build failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	send_ap_command(s->conn_handle, ALIRO_INS_AUTH0, apdu, n);
	s->phase = PH_SENT_AUTH0;
}

// Handles an inbound AUTH0Response: strips the APDU status word, parses the device's ephemeral
// public key, performs ECDH with the reader's ephemeral private key, derives the KDF intermediate
// z, signs the reader-usage transcript, and sends AUTH1. On any failure (short/malformed APDU,
// parse failure, ECDH failure, signing failure) sets s->phase to PH_FAILED and returns without
// sending. On success sets s->phase to PH_SENT_AUTH1 after sending the AUTH1 command. Logs (does
// not fail on) an unexpected status word other than 0x9000.
static void on_auth0_response(struct aliro_session *s, const uint8_t *pl, size_t len)
{
	// Holds the fields parsed from an AUTH0Response APDU while it is being processed by the
	// reader's response handler.
	struct aliro_auth0_response r;
	uint16_t sw;

	/* APDU response = <response TLV> SW1 SW2; drop the status word before parsing. */
	if (aliro_apdu_strip_sw(pl, &len, &sw) != 0) {
		LOG_ERR("[conn %u] AUTH0Response too short (%u B)", s->conn_handle, (unsigned)len);
		s->phase = PH_FAILED;
		return;
	}
	if (sw != 0x9000u) {
		LOG_WRN("[conn %u] AUTH0Response SW=0x%04x (expected 0x9000)", s->conn_handle, sw);
	}
	if (aliro_apdu_parse_auth0_response(pl, len, &r) != 0) {
		LOG_ERR("[conn %u] AUTH0Response parse failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	memcpy(s->device_eph_pub, r.device_eph_pub, ALIRO_P256_POINT);

	uint8_t shared[ALIRO_SHARED_SECRET_LEN];

	if (aliro_ecdh_p256(s->reader_eph_priv, s->device_eph_pub, shared) != 0) {
		LOG_ERR("[conn %u] ECDH failed", s->conn_handle);
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
		LOG_ERR("[conn %u] reader signature failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	if (aliro_apdu_build_auth1(0x01u, sig, apdu, sizeof(apdu), &n) != 0) {
		s->phase = PH_FAILED;
		return;
	}
	send_ap_command(s->conn_handle, ALIRO_INS_AUTH1, apdu, n);
	s->phase = PH_SENT_AUTH1;
}

// Handles an inbound AUTH1Response: derives the AP and BLE-ranging secure channel keys and URSK
// from the ECDH intermediate, decrypts and parses the response, verifies the device's signature,
// checks trust, and sends EXCHANGE. Requires the reader group X coordinate to already be available
// (s_have_group_x); fails otherwise. Derives the session salt and 160-byte key block, splits it
// into the AP channel keys and URSK, and separately derives the BLE ranging-channel keys from the
// block's BleSK segment using a versions-based salt; both secure channels are initialized with
// counters starting at 1. Decrypts the AUTH1Response body via AES-GCM and fails on tag mismatch
// (indicating a key/counter/framing error), oversized ciphertext, or parse failure. Verifies the
// device's signature over the device-usage transcript using the presented device public key if
// available, else the device's ephemeral public key; a bad signature fails the session. Records the
// presented credential key under s_prov_lock and checks it against the trust store: an untrusted
// key fails the session unless the reader identity is the dev default (which accepts and warns). On
// success, seals and sends the EXCHANGE command, sets s->phase to PH_SENT_EXCHANGE, and logs the
// derived URSK; on any failure path sets s->phase to PH_FAILED and returns without sending
// EXCHANGE.
static void on_auth1_response(struct aliro_session *s, const uint8_t *pl, size_t len)
{
	// Holds the fields parsed from an AUTH1Response APDU while it is being processed by the
	// reader's response handler.
	struct aliro_auth1_response r;
	uint16_t sw;

	/* APDU response = <response TLV> SW1 SW2; drop the status word before parsing. */
	if (aliro_apdu_strip_sw(pl, &len, &sw) != 0) {
		LOG_ERR("[conn %u] AUTH1Response too short (%u B)", s->conn_handle, (unsigned)len);
		s->phase = PH_FAILED;
		return;
	}
	if (sw != 0x9000u) {
		LOG_WRN("[conn %u] AUTH1Response SW=0x%04x (expected 0x9000)", s->conn_handle, sw);
	}
	/* Establish the secure channel BEFORE reading the body: the AUTH1Response is
	 * AES-256-GCM-encrypted under it (Aliro §8.3.1.6/.7). salt_volatile (§8.3.1.13)
	 * = reader_group_key.x || "Volatile****" || reader_id || interface_byte(BLE) ||
	 * 0x5C 0x02 || version || reader_eph.x || txid || flag || phone 0xA5 TLV; info =
	 * the device (Access Credential) ephemeral pub X; ikm = Kdh (s->z). The URSK and
	 * the ExpeditedSKReader/Device channel keys fall out of the same 160-byte block. */
	uint8_t salt[ALIRO_SALT_MAX], block[ALIRO_KEY_BLOCK_LEN];
	uint8_t enc[ALIRO_SESSION_KEY_LEN], dec[ALIRO_SESSION_KEY_LEN];
	size_t slen;
	const uint8_t *a5 = s->a5_len ? s->a5_tlv : k_a5_csa_v1;
	size_t a5n = s->a5_len ? s->a5_len : sizeof(k_a5_csa_v1);

	if (!s_have_group_x) {
		LOG_ERR("[conn %u] no reader group key; cannot build session salt", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	if (aliro_salt_build(ALIRO_SALT_SESSION, s->txid, s_reader_group_x, s->reader_eph_pub + 1,
			     s_id.reader_id, ALIRO_IFACE_BLE, ALIRO_VERSION, 0x00u, 0x01u, NULL, a5,
			     a5n, salt, &slen) != 0 ||
	    aliro_crypto_derive_block(s->z, salt, slen, s->device_eph_pub + 1, block) != 0) {
		LOG_ERR("[conn %u] key-block derivation failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	aliro_crypto_split(block, 1, enc, dec, s->ursk);
	aliro_secchan_init(&s->sc, enc, dec);

	/* Ranging channel keys (§11.8.1): BleSKReader/BleSKDevice off BleSK (block
	 * offset 96); salt = reader_supported_versions || user_device_selected_version.
	 * We advertise and select v1.0 only, so salt = 01 00 01 00. Its own counters
	 * (fresh from 1) live in s->sc_ble and carry across AP-Completed + M1..M4. */
	{
		uint8_t ble_r[ALIRO_SESSION_KEY_LEN], ble_d[ALIRO_SESSION_KEY_LEN];
		uint8_t ble_salt[sizeof(k_proto_versions) + 2];
		size_t bsl = 0;

		for (size_t i = 0; i < sizeof(k_proto_versions) / sizeof(k_proto_versions[0]);
		     i++) {
			ble_salt[bsl++] = (uint8_t)(k_proto_versions[i] >> 8);
			ble_salt[bsl++] = (uint8_t)(k_proto_versions[i] & 0xffu);
		}
		ble_salt[bsl++] = (uint8_t)(ALIRO_VERSION >> 8); /* selected = only version */
		ble_salt[bsl++] = (uint8_t)(ALIRO_VERSION & 0xffu);
		if (aliro_crypto_derive_ble_keys(block, ble_salt, bsl, ble_r, ble_d) != 0) {
			LOG_ERR("[conn %u] BleSK derivation failed", s->conn_handle);
			s->phase = PH_FAILED;
			return;
		}
		aliro_secchan_init(&s->sc_ble, ble_r, ble_d);
	}

	/* Decrypt the body: <ciphertext || 16-byte GCM tag>, the first inbound message
	 * (dec counter 1 per §8.3.1.13 init). A tag mismatch means the session key,
	 * counter, or ct/tag framing is off — the on-hardware test of the key schedule. */
	if (len < ALIRO_GCM_TAG_LEN) {
		LOG_ERR("[conn %u] AUTH1Response too short for a GCM tag (%u B)", s->conn_handle,
			(unsigned)len);
		s->phase = PH_FAILED;
		return;
	}
	size_t ctlen = len - ALIRO_GCM_TAG_LEN;
	uint8_t ptbuf[256];

	if (ctlen > sizeof(ptbuf)) {
		LOG_ERR("[conn %u] AUTH1Response too large (%u B)", s->conn_handle,
			(unsigned)ctlen);
		s->phase = PH_FAILED;
		return;
	}
	if (aliro_secchan_open(&s->sc, NULL, 0, pl, ctlen, pl + ctlen, ptbuf) != 0) {
		LOG_ERR("[conn %u] AUTH1Response GCM auth FAILED (%u ct B): session key / "
			"counter / ct-tag framing mismatch",
			s->conn_handle, (unsigned)ctlen);
		s->phase = PH_FAILED;
		return;
	}
	LOG_INF("[conn %u] AUTH1Response DECRYPTED (%u B plaintext):", s->conn_handle,
		(unsigned)ctlen);
	LOG_HEXDUMP_INF(ptbuf, ctlen, "");

	if (aliro_apdu_parse_auth1_response(ptbuf, ctlen, &r) != 0) {
		LOG_ERR("[conn %u] AUTH1Response (decrypted) parse failed", s->conn_handle);
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
		LOG_WRN("[conn %u] device signature INVALID (may need key lookup by "
			"identifier; the decrypt itself succeeded)",
			s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	LOG_INF("[conn %u] device signature OK", s->conn_handle);

	/* Remember the presented credential key for the `aliro-trust` bench command,
	 * and take the trust decision, under the lock the REPL commands share. The
	 * raw-key allowlist is the interim seam; real issuer-chain validation plugs
	 * in here (Phase 4). */
	woz_mutex_lock(&s_prov_lock);
	memcpy(s_last_cred_pub, cred_pub, ALIRO_CRED_PUB_LEN);
	s_have_last_cred = true;
	int tv = aliro_prov_trust_check(&s_trust, cred_pub);
	woz_mutex_unlock(&s_prov_lock);

	if (tv == 0) {
		LOG_INF("[conn %u] credential key TRUSTED", s->conn_handle);
	} else if (tv == 1 && s_id.is_dev) {
		LOG_WRN("[conn %u] no trust anchors (DEV identity): accepting the "
			"presented credential; run `aliro-trust` to enforce",
			s->conn_handle);
	} else {
		LOG_WRN("[conn %u] credential key NOT trusted (%s); rejecting", s->conn_handle,
			tv == 1 ? "no anchors provisioned" : "not in trust store");
		s->phase = PH_FAILED;
		return;
	}

	/* Accepted: remember which key it was, so the unlock this session goes on to
	 * grant can be attributed to the Matter user that owns it. */
	woz_mutex_lock(&s_prov_lock);
	memcpy(s_auth_cred_pub, cred_pub, ALIRO_CRED_PUB_LEN);
	s_have_auth_cred = true;
	woz_mutex_unlock(&s_prov_lock);

	/* EXCHANGE: seal the URSK-ready trigger and frame it. */
	uint8_t ex[16], ct[16], tag[ALIRO_GCM_TAG_LEN], payload[32];
	size_t exn;

	if (aliro_apdu_build_exchange(0, 0, 1, ex, sizeof(ex), &exn) != 0 ||
	    aliro_secchan_seal(&s->sc, NULL, 0, ex, exn, ct, tag) != 0) {
		LOG_ERR("[conn %u] EXCHANGE seal failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	memcpy(payload, ct, exn);
	memcpy(payload + exn, tag, ALIRO_GCM_TAG_LEN);
	send_ap_command(s->conn_handle, ALIRO_INS_EXCHANGE, payload, exn + ALIRO_GCM_TAG_LEN);

	/* Auth + both channels + URSK are ready. Wait for the EXCHANGE response before
	 * completing the AP: §11.1.1 requires the reader to send Reader-Status-AP-Completed
	 * (BleSK-sealed) after EXCHANGE succeeds, otherwise the device stalls and drops
	 * (URSK_Unavailable). on_exchange_response drives that + ranging. */
	s->phase = PH_SENT_EXCHANGE;
	LOG_INF("[conn %u] URSK derived; EXCHANGE sent, awaiting response", s->conn_handle);
	LOG_HEXDUMP_INF(s->ursk, ALIRO_URSK_LEN, "");
}

/* Reader-Status-Access-Protocol-Completed (§11.1.1 / §11.7.3.4.1): a proto-2
 * Notification, message-id 0x03, carrying one Reader Information Attribute
 * (id 0x00, len 2, value = unsolicited-status-reporting mode in bits 15:13). The
 * device will not initiate ranging until it receives this. Plaintext message the
 * BleSK channel then seals: [02][03][00 04][00 02 20 00]. */
static const uint8_t k_ap_completed_plain[] = {
	0x02, 0x03, 0x00, 0x04, 0x00, 0x02, 0x20, 0x00,
};

/* Handle the EXCHANGE response, then complete the AP and arm ranging. The body is
 * an AP (proto-0) response on the ExpeditedSK channel: <ct || 16B tag> SW1SW2. */
static void on_exchange_response(struct aliro_session *s, const uint8_t *pl, size_t len)
{
	uint16_t sw;

	if (aliro_apdu_strip_sw(pl, &len, &sw) != 0) {
		LOG_ERR("[conn %u] EXCHANGE response too short", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	uint8_t body[16];
	size_t bodylen = (len >= ALIRO_GCM_TAG_LEN) ? len - ALIRO_GCM_TAG_LEN : 0;

	if (bodylen == 0 || bodylen > sizeof(body) ||
	    aliro_secchan_open(&s->sc, NULL, 0, pl, bodylen, pl + bodylen, body) != 0) {
		LOG_ERR("[conn %u] EXCHANGE response decrypt failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	/* Success body = 00 02 00 00 (len 0x0002, error 0x0000). Non-zero error bytes
	 * (body[2..4]) mean the device rejected a request. */
	bool ok = bodylen >= 4u && body[2] == 0x00u && body[3] == 0x00u;

	LOG_INF("[conn %u] EXCHANGE response: %s", s->conn_handle,
		ok ? "success (URSK armed)" : "ERROR");
	LOG_HEXDUMP_INF(body, bodylen, "");
	if (!ok) {
		s->phase = PH_FAILED;
		return;
	}

	/* Reader-Status-AP-Completed, BleSK-sealed on the ranging channel (counter 1). */
	uint8_t wire[64];
	size_t wl;

	if (aliro_msg_seal(&s->sc_ble, k_ap_completed_plain, sizeof(k_ap_completed_plain), wire,
			   sizeof(wire), &wl) != 0) {
		LOG_ERR("[conn %u] AP-Completed seal failed", s->conn_handle);
		s->phase = PH_FAILED;
		return;
	}
	int rc = aliro_ble_send(s->conn_handle, wire, wl);

	LOG_INF("[conn %u] Reader-Status-AP-Completed sent (%u B, rc=%d)", s->conn_handle,
		(unsigned)wl, rc);

	/* Arm the ranging engine with the URSK + the BleSK channel; M1 is emitted by the
	 * engine when the device sends its Initiate-Ranging-Session (proto-2 id-1). The
	 * ranging session id is NOT reader's free choice: the device derives it as the
	 * big-endian last 4 bytes of the 16-byte AUTH0 transaction id (libaliro
	 * AccessProtocolCrypto::GetSessionId = rev(txid[12..15])) and indexes its URSK by
	 * it. A mismatch makes M1 land on a device with no URSK for that session
	 * (GeneralError URSK_Unavailable). */
	uint32_t ranging_sid = ((uint32_t)s->txid[12] << 24) | ((uint32_t)s->txid[13] << 16) |
			       ((uint32_t)s->txid[14] << 8) | (uint32_t)s->txid[15];

	s->phase = PH_ESTABLISHED;
	if (aliro_ranging_start(s->conn_handle, ranging_sid, s->ursk, &s->sc_ble) != 0) {
		LOG_WRN("[conn %u] ranging setup did not arm", s->conn_handle);
	}
}

/* Reader Status Changed (Aliro transaction step 23): the reader->phone grant/relock
 * confirmation that fires the iPhone Wallet unlock animation. proto-2 (Notification)
 * message-id 0x02, one State Attribute (id 0x00, len 2) = [OperationSource,
 * ReaderStateByte]. OperationSource 0x04 = this user device in the BLE+UWB Aliro flow;
 * ReaderStateByte Unsecured 0x01 = granted (animate), Secured 0x00 = relocked. The
 * 65-byte access-credential public key is NOT serialized (the reference uses it only
 * to select which connection to notify). Plaintext the BleSK channel then seals:
 * [02][02][00 04][00 02 04 <state>]. Runs on the BLE-host task (posted via
 * aliro_ble_post_reader_status) so it serializes with the other sc_ble seals. */
static void reader_status_send_on_host(bool unsecured)
{
	struct aliro_session *s = NULL;

	for (int i = 0; i < ALIRO_MAX_SESSIONS; i++) {
		if (s_sessions[i].active && s_sessions[i].phase == PH_ESTABLISHED) {
			s = &s_sessions[i];
			break;
		}
	}
	if (s == NULL) {
		LOG_WRN("reader-status-changed: no established session to notify");
		return;
	}

	const uint8_t plain[8] = {
		0x02u, 0x02u, 0x00u, 0x04u,
		0x00u, 0x02u, 0x04u, (uint8_t)(unsecured ? 0x01u : 0x00u),
	};
	uint8_t wire[64];
	size_t wl;

	if (aliro_msg_seal(&s->sc_ble, plain, sizeof(plain), wire, sizeof(wire), &wl) != 0) {
		LOG_ERR("[conn %u] reader-status-changed seal failed", s->conn_handle);
		return;
	}
	int rc = aliro_ble_send(s->conn_handle, wire, wl);

	LOG_INF("[conn %u] Reader-Status-Changed %s sent (%u B, rc=%d)", s->conn_handle,
		unsecured ? "Unsecured/grant" : "Secured/relock", (unsigned)wl, rc);
}

// Sends a Reader-Status BLE notification reporting the lock's unsecured/secured state to the
// connected device. unsecured is true if the reader/lock is currently unsecured (unlocked), false
// if secured.
void aliro_reader_notify_unlock(bool unsecured)
{
	aliro_ble_post_reader_status(reader_status_send_on_host, unsecured);
}

// Copies the credential public key that most recently passed the trust check into out.
// Returns true if a credential has authenticated since boot (out written), false otherwise
// (out untouched). Safe to call from any task.
bool aliro_reader_authenticated_credential(uint8_t out[ALIRO_CRED_PUB_LEN])
{
	bool have;

	load_provisioning(); /* the Matter task can reach here before the reader started */

	woz_mutex_lock(&s_prov_lock);
	have = s_have_auth_cred;
	if (have) {
		memcpy(out, s_auth_cred_pub, ALIRO_CRED_PUB_LEN);
	}
	woz_mutex_unlock(&s_prov_lock);

	return have;
}

/* Scan an op-0x05 Initiate-Access-Protocol payload for the phone's 0xA5
 * proprietary-information TLV (short-form BER length; the A5 value is small) and
 * copy the whole TLV (tag+len+value) into out. Returns the stored length, or 0
 * if no well-formed 0xA5 TLV fits. */
static size_t capture_a5_tlv(const uint8_t *pl, size_t pl_len, uint8_t *out, size_t cap)
{
	for (size_t i = 0; i + 2u <= pl_len; i++) {
		if (pl[i] != 0xA5u) {
			continue;
		}
		size_t vlen = pl[i + 1]; /* short-form length only */
		size_t tlv = 2u + vlen;

		if (vlen < 0x80u && i + tlv <= pl_len && tlv <= cap) {
			memcpy(out, pl + i, tlv);
			return tlv;
		}
	}
	return 0;
}

/* Consume one inbound Aliro transaction SDU. */
static void transaction_feed(struct aliro_session *s, const uint8_t *data, uint16_t len)
{
	s->msgs_rx++;

	uint8_t type, opcode;
	const uint8_t *pl;
	size_t pl_len;

	if (aliro_ble_unframe(data, len, &type, &opcode, &pl, &pl_len) != 0) {
		LOG_WRN("[conn %u] msg #%u (%u B): not a valid envelope", s->conn_handle,
			(unsigned)s->msgs_rx, (unsigned)len);
		LOG_HEXDUMP_INF(data, len, "");
		pl = data;
		pl_len = len;
		type = 0xff;
		opcode = 0xff;
	}
	LOG_INF("[conn %u] msg #%u: type=0x%02x op=0x%02x, %u payload B, phase=%s", s->conn_handle,
		(unsigned)s->msgs_rx, type, opcode, (unsigned)pl_len, phase_str(s->phase));

	/* A Notification Event mid-auth is the device rejecting us: GeneralError is
	 * encoded [01 01 <code>]. Surface <code> — it is the exact reason the phone
	 * bailed, far more legible than a downstream parse failure. Only pre-ranging:
	 * once ESTABLISHED, proto-2 events ride the BleSK channel encrypted, so pl[] is
	 * ciphertext here — the real event must be opened below, not read raw. */
	if (s->phase != PH_ESTABLISHED && type == ALIRO_PROTO_NOTIFICATION &&
	    opcode == ALIRO_NOTIF_EVENT) {
		uint8_t code = (pl_len >= 3u) ? pl[2] : 0xffu;

		LOG_WRN("[conn %u] device GeneralError 0x%02x in phase %s", s->conn_handle, code,
			phase_str(s->phase));
		s->phase = PH_FAILED;
		return;
	}

	switch (s->phase) {
	case PH_IDLE:
		/* First peer message = op-0x05 Initiate Access Protocol, carrying the
		 * phone's 0xA5 proprietary-info TLV. Capture that TLV for the session-key
		 * salt (§8.3.1.13 trailing field) before driving AUTH0; fall back to the
		 * CSA v1.0 default if the phone sent none. Version negotiation rides the
		 * GATT characteristic. */
		s->a5_len = capture_a5_tlv(pl, pl_len, s->a5_tlv, sizeof(s->a5_tlv));
		if (s->a5_len == 0) {
			LOG_WRN("[conn %u] no 0xA5 TLV in op-0x05; salt will use CSA v1.0 default",
				s->conn_handle);
		} else {
			LOG_INF("[conn %u] captured phone 0xA5 TLV (%u B) for session salt",
				s->conn_handle, (unsigned)s->a5_len);
		}
		LOG_INF("[conn %u] peer opened; starting access protocol", s->conn_handle);
		start_auth(s);
		break;
	case PH_SENT_AUTH0:
		on_auth0_response(s, pl, pl_len);
		break;
	case PH_SENT_AUTH1:
		on_auth1_response(s, pl, pl_len);
		break;
	case PH_SENT_EXCHANGE:
		on_exchange_response(s, pl, pl_len);
		break;
	case PH_ESTABLISHED: {
		/* Ranging SDUs (proto 1/2/3) ride the BleSK channel: open the whole SDU
		 * (<hdr><ct||16B tag>), dump the plaintext, and hand it to the engine, which
		 * emits M1 on the Initiate-Ranging-Session and M3 on M2. The engine's replies
		 * are BleSK-sealed by aliro_ranging's tx callback. */
		uint8_t plain[256];
		size_t plen;

		if (aliro_msg_open(&s->sc_ble, data, len, plain, sizeof(plain), &plen) != 0) {
			LOG_WRN("[conn %u] ranging SDU open FAILED (proto=0x%02x id=0x%02x, %u B); "
				"raw:",
				s->conn_handle, type, opcode, (unsigned)pl_len);
			LOG_HEXDUMP_INF(pl, pl_len, "");
			break;
		}
		LOG_INF("[conn %u] ranging SDU (proto=0x%02x id=0x%02x, %u B plaintext):",
			s->conn_handle, plain[0], plain[1], (unsigned)plen);
		LOG_HEXDUMP_INF(plain, plen, "");
		aliro_ranging_feed(s->conn_handle, plain, plen);
		break;
	}
	default:
		LOG_WRN("[conn %u] message in phase %s ignored", s->conn_handle,
			phase_str(s->phase));
		break;
	}
}

/* ---- aliro_ble transport callbacks ---- */

// BLE connection-established callback: allocates a session slot for the new connection.
// Logs an error and returns without effect if no free session slot is available.
static void on_connected(uint16_t conn_handle)
{
	struct aliro_session *s = session_alloc(conn_handle);

	if (s == NULL) {
		LOG_ERR("[conn %u] no free session slot", conn_handle);
		return;
	}
	LOG_INF("[conn %u] Aliro session created", conn_handle);
}

// BLE disconnection callback: marks the connection's session inactive (if one exists) and
// stops any UWB ranging associated with the connection.
// Logs the session's message count and final transaction phase before deactivating it.
static void on_disconnected(uint16_t conn_handle)
{
	struct aliro_session *s = session_find(conn_handle);

	if (s != NULL) {
		LOG_INF("[conn %u] Aliro session destroyed (%u msgs, phase=%s)", conn_handle,
			(unsigned)s->msgs_rx, phase_str(s->phase));
		s->active = false;
	}
	aliro_ranging_stop(conn_handle);
}

// BLE data-received callback: looks up the session for conn_handle and feeds the data into its
// transaction state machine.
// Logs a warning and drops the data if no active session exists for conn_handle.
static void on_data(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
	struct aliro_session *s = session_find(conn_handle);

	if (s == NULL) {
		LOG_WRN("[conn %u] data for unknown session (%u bytes)", conn_handle,
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
		LOG_ERR("crypto init failed");
		return -1;
	}
	load_provisioning();
	if (aliro_ranging_init() != 0) {
		LOG_WRN("UWB ranging adapter unavailable; auth will run but "
			"ranging setup won't start");
	}
	return 0;
}

// Starts the Aliro reader: initializes the engine (crypto, provisioning, UWB ranging) and brings up
// the BLE transport using the default advertising config. Returns 0 on success; returns -1 if
// engine initialization fails, or the underlying aliro_ble_start result otherwise.
int aliro_reader_start(void)
{
	if (reader_engine_init() != 0) {
		return -1;
	}
	struct aliro_ble_config cfg = make_ble_cfg();
	int rc = aliro_ble_start(&cfg);

	LOG_INF("aliro_reader_start: transport %s (SPSM 0x%04x)", rc == 0 ? "up" : "FAILED",
		aliro_ble_spsm());
	return rc;
}

/* ---- attach mode: share a host another stack (e.g. Matter) owns ---------- */

// Prepares the BLE transport and returns the Aliro GATT service definition for external
// registration, without starting the transport. Returns NULL if aliro_ble_prepare fails; on success
// returns the pointer from aliro_ble_service_def(), owned by the BLE layer.
const void *aliro_reader_ble_prepare(void)
{
	struct aliro_ble_config cfg = make_ble_cfg();

	if (aliro_ble_prepare(&cfg) != 0) {
		LOG_ERR("aliro_ble_prepare failed");
		return NULL;
	}
	return aliro_ble_service_def();
}

// Starts the Aliro reader in "attached" transport mode: initializes the engine, applies provisioned
// resolvable advertising parameters if a real GRK is present, then starts the attached BLE
// transport. Unlike aliro_reader_start, this applies GRK-based advertising params (group/subgroup
// ID from reader_id, GRK) before starting, when the reader has already been provisioned; falls back
// to unresolvable advertising if no GRK is set yet. Returns 0 on success; returns -1 if engine
// initialization fails, or the underlying aliro_ble_start_attached result otherwise.
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
		const uint8_t sub2[2] = {s_id.reader_id[16], s_id.reader_id[17]};

		aliro_ble_set_adv_params(&s_id.reader_id[0], sub2, s_id.grk, 0 /* tx power */);
	}

	int rc = aliro_ble_start_attached();

	LOG_INF("aliro_reader_start_attached: %s (SPSM 0x%04x)", rc == 0 ? "up" : "FAILED",
		aliro_ble_spsm());
	return rc;
}

// Refreshes the BLE advertisement to include the resolvable service data once a real
// GroupResolvingKey (GRK) is available. Handles the case where Matter provisioning
// (SetAliroReaderConfig) lands after advertising has already started with only the bare 0xFFF2 UUID
// (dev default, all-zero GRK), which the phone cannot resolve. No-ops if the GRK in s_id is still
// all-zero. On a nonzero GRK, derives the two-byte subgroup ID from reader_id[16..17] and calls
// aliro_ble_set_adv_params + aliro_ble_readvertise to make the reader approach-resolvable.
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
	const uint8_t sub2[2] = {s_id.reader_id[16], s_id.reader_id[17]};

	aliro_ble_set_adv_params(&s_id.reader_id[0], sub2, s_id.grk, 0 /* tx power */);
	aliro_ble_readvertise();
	LOG_INF("advertisement refreshed with provisioned GRK (approach-resolvable)");
}

/* ---- bench provisioning helpers (aliro-prov / aliro-trust) ------------- */

// Print the reader's provisioning state (identity, trust anchors, last presented credential)
// to the console for diagnostics.
// Loads provisioning first, then snapshots the shared state under s_prov_lock before printing
// so UART I/O does not hold the lock during the BLE task's trust check.
void aliro_reader_prov_print(void)
{
	load_provisioning();

	/* Snapshot the shared state under the lock, then print off the copy so the
	 * UART I/O never holds up the BLE task's trust check. */
	struct aliro_trust_store ts;
	uint8_t last[ALIRO_CRED_PUB_LEN];
	bool have;

	woz_mutex_lock(&s_prov_lock);
	ts = s_trust;
	have = s_have_last_cred;
	memcpy(last, s_last_cred_pub, ALIRO_CRED_PUB_LEN);
	woz_mutex_unlock(&s_prov_lock);

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

// Add the most recently presented credential's public key to the trust store and persist it.
// Returns 1 if no credential has been presented yet or it is already trusted (nothing
// persisted), -1 if the store is full or the NVS write fails (in-memory trust store left
// unchanged on failure), 0 if newly added and committed.
int aliro_reader_trust_last(void)
{
	load_provisioning();

	/* Build the candidate store off a snapshot, persist it, then commit — so the
	 * NVS write happens outside the lock and a failed write changes nothing. */
	struct aliro_trust_store cand;
	uint8_t last[ALIRO_CRED_PUB_LEN];
	bool have;

	woz_mutex_lock(&s_prov_lock);
	cand = s_trust;
	have = s_have_last_cred;
	memcpy(last, s_last_cred_pub, ALIRO_CRED_PUB_LEN);
	woz_mutex_unlock(&s_prov_lock);

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
	woz_mutex_lock(&s_prov_lock);
	s_trust = cand;
	woz_mutex_unlock(&s_prov_lock);
	return 0;
}

/* ---- Matter provisioning bridge (Phase 4) ------------------------------ *
 * These run on the Matter task during commissioning, before the reader is
 * started (the reader starts only after the BLE handoff). They mutate the same
 * s_id/s_trust the reader loads, and persist through aliro_prov, so a snapshot-
 * then-store keeps NVS and the in-memory copy consistent under s_prov_lock. */

// Store a Matter-provisioned reader identity (reader ID, signing private key, GRK), keeping
// any trust anchors already present, and persist it to NVS.
// Returns -1 if the NVS write fails, in which case in-memory identity (s_id) is unchanged;
// returns 0 on success, after which the reader group key salt is recomputed via
// compute_reader_group_x since the signing key changed.
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

	woz_mutex_lock(&s_prov_lock);
	ts = s_trust; /* keep any anchors already added */
	woz_mutex_unlock(&s_prov_lock);

	if (aliro_prov_store(&id, &ts) != 0) {
		return -1; /* not committed; s_id unchanged */
	}
	woz_mutex_lock(&s_prov_lock);
	s_id = id;
	woz_mutex_unlock(&s_prov_lock);
	compute_reader_group_x(); /* signingKey changed -> refresh salt field 1 */
	LOG_INF("Matter-provisioned reader identity stored");
	return 0;
}

// Add a Matter-provisioned credential public key to the reader's trust store and persist it.
// Returns 0 if newly added and stored, 1 if the credential was already trusted (nothing
// persisted), -1 if the store is full, cred_pub is not a valid P-256 point, or the NVS write
// fails. On failure the in-memory trust store (s_trust) is left unchanged.
int aliro_reader_provision_add_trust(const uint8_t cred_pub[ALIRO_CRED_PUB_LEN])
{
	load_provisioning();

	struct aliro_reader_identity id;
	struct aliro_trust_store cand;

	woz_mutex_lock(&s_prov_lock);
	id = s_id;
	cand = s_trust;
	woz_mutex_unlock(&s_prov_lock);

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
	woz_mutex_lock(&s_prov_lock);
	s_trust = cand;
	woz_mutex_unlock(&s_prov_lock);
	LOG_INF("Matter-provisioned trust anchor stored (%u total)", cand.count);
	return 0;
}

// Revert the reader's provisioning to the default dev identity and empty trust store, and
// persist that state to NVS.
// Returns -1 if the NVS write fails, in which case in-memory state is unchanged; returns 0 on
// success, after which the reader group key salt is recomputed via compute_reader_group_x.
int aliro_reader_provision_clear(void)
{
	load_provisioning();

	struct aliro_reader_identity id;
	struct aliro_trust_store ts;

	aliro_prov_dev_default(&id, &ts);
	if (aliro_prov_store(&id, &ts) != 0) {
		return -1;
	}
	woz_mutex_lock(&s_prov_lock);
	s_id = id;
	s_trust = ts;
	woz_mutex_unlock(&s_prov_lock);
	compute_reader_group_x(); /* signingKey changed -> refresh salt field 1 */
	LOG_INF("reader provisioning cleared (reverted to dev identity)");
	return 0;
}
