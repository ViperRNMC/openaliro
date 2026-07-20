// APDU framing and parsing for the Aliro Access Protocol: builds outbound command APDUs via a
// TLV writer and parses the AUTH0/AUTH1 response APDUs exchanged during the reader-device
// handshake.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_apdu — the Aliro credential-auth wire codec: single-byte-tag BER-TLV
 * plus the AUTH0/AUTH1 command builders, the ECDSA authentication-data
 * transcript, the AUTH0/AUTH1 response parsers, the EXCHANGE command, and the
 * 4-byte L2CAP envelope. Pure byte manipulation, no crypto and no platform
 * dependency, so it is host-KAT verifiable against the recovered layouts.
 *
 * Provenance: clean-room. Byte layouts from the project's reverse-engineering
 * notes; the code is original.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- BLE ProtocolType (envelope byte 0) ---- */
#define ALIRO_PROTO_ACCESS       0x00u /* Access Protocol: payload is an ISO7816 APDU */
#define ALIRO_PROTO_NOTIFICATION 0x02u /* Notification: Initiate-AP / Event */

/* ---- Access-Protocol opcode (envelope byte 1, directional) ---- */
#define ALIRO_AP_OP_COMMAND  0x00u /* reader -> device APDU command */
#define ALIRO_AP_OP_RESPONSE 0x01u /* device -> reader APDU response (<tlv> SW1 SW2) */

/* ---- Notification opcode (envelope byte 1 when type == NOTIFICATION) ---- */
#define ALIRO_NOTIF_EVENT       0x00u /* Event, e.g. GeneralError [01 01 <code>] */
#define ALIRO_NOTIF_INITIATE_AP 0x05u /* Initiate Access Protocol (phone's first msg) */

/* ---- APDU instruction bytes (INS in "80 INS 00 00 Lc <tlv> Le") ----
 * NOT BLE opcodes: every AP command frames as type=ACCESS, opcode=AP_OP_COMMAND,
 * and carries the ISO7816 APDU whose INS selects the command. */
#define ALIRO_INS_AUTH0    0x80u
#define ALIRO_INS_AUTH1    0x81u
#define ALIRO_INS_EXCHANGE 0xC9u

/* ---- TLV tags ---- */
#define ALIRO_TAG_EXP_PHASE  0x41u /* AUTH0: ExpeditedPhaseType; AUTH1: AccessCredentialType */
#define ALIRO_TAG_USER_POL   0x42u /* AUTH0: UserAuthenticationPolicy */
#define ALIRO_TAG_VERSION    0x5Cu /* AUTH0: protocol version u16 BE */
#define ALIRO_TAG_READER_EPH 0x87u /* reader ephemeral pubkey (65) / transcript pubX (32) */
#define ALIRO_TAG_TXID       0x4Cu /* transaction identifier (16) */
#define ALIRO_TAG_READER_ID  0x4Du /* reader identifier (32) */
#define ALIRO_TAG_SIG        0x9Eu /* ECDSA signature r|s (64) */
#define ALIRO_TAG_DEVICE_PUBX                                                                      \
	0x86u                      /* transcript device pubX (32) / AUTH0Resp device eph pub (65) */
#define ALIRO_TAG_USAGE      0x93u /* transcript usage domain separator (4) */
#define ALIRO_TAG_DEVICE_PUB 0x5Au /* AUTH1Resp device public key (65) */
#define ALIRO_TAG_STATUS     0x97u /* EXCHANGE ReaderStatus (u16 BE) */
#define ALIRO_TAG_URSK_READY 0x98u /* EXCHANGE URSK-ready trigger (zero length) */

/* ---- BER-TLV writer ---- */
struct aliro_tlv_w {
	uint8_t *buf;
	size_t cap;
	size_t len;
	int err;
};

void aliro_tlv_w_init(struct aliro_tlv_w *w, uint8_t *buf, size_t cap);
void aliro_tlv_put(struct aliro_tlv_w *w, uint8_t tag, const uint8_t *val, size_t len);
void aliro_tlv_put_u8(struct aliro_tlv_w *w, uint8_t tag, uint8_t v);
void aliro_tlv_put_u16(struct aliro_tlv_w *w, uint8_t tag, uint16_t v); /* big-endian */
void aliro_tlv_put_empty(struct aliro_tlv_w *w, uint8_t tag);           /* zero length */
int aliro_tlv_w_finish(struct aliro_tlv_w *w, size_t *out_len);         /* 0 ok, -1 overflow */

/* Find the first item with tag; returns 0 and sets val/len, or -1 if absent. */
int aliro_tlv_find(const uint8_t *buf, size_t buf_len, uint8_t tag, const uint8_t **val,
		   size_t *val_len);

/* ---- command builders (out receives the raw APDU payload, no envelope) ---- */
int aliro_apdu_build_auth0(uint8_t exp_phase, uint8_t user_policy, uint16_t version,
			   const uint8_t reader_eph_pub[65], const uint8_t txid[16],
			   const uint8_t reader_id[32], uint8_t *out, size_t cap, size_t *out_len);
int aliro_apdu_build_auth1(uint8_t cred_type, const uint8_t sig[64], uint8_t *out, size_t cap,
			   size_t *out_len);

/* The ECDSA transcript that is signed (reader) / verified (device). which:
 * 1 = reader authenticates itself (kReaderUsage); 0 = verify user device
 * (kUserDeviceUsage). Spans: device pubX (0x86) then reader-eph pubX (0x87). */
#define ALIRO_AUTH_READER 1
#define ALIRO_AUTH_DEVICE 0
int aliro_apdu_build_authdata(int which, const uint8_t reader_id[32], const uint8_t device_pubx[32],
			      const uint8_t reader_eph_pubx[32], const uint8_t txid[16],
			      uint8_t *out, size_t cap, size_t *out_len);

/* EXCHANGE command plaintext (sealed by the caller before framing). */
int aliro_apdu_build_exchange(int have_status, uint16_t reader_status, int ursk_ready, uint8_t *out,
			      size_t cap, size_t *out_len);

/* Wrap a command TLV in an ISO7816 short-form APDU: "80 <ins> 00 00 Lc <tlv> Le"
 * (Le = 0x00 => up to 256 response bytes). ins is one of ALIRO_INS_*. The result
 * is the AP command payload to frame with type=ACCESS, opcode=AP_OP_COMMAND. */
int aliro_apdu_wrap(uint8_t ins, const uint8_t *tlv, size_t tlv_len, uint8_t *out, size_t cap,
		    size_t *out_len);

/* ---- response parsers ---- */

/* Strip the trailing 2-byte ISO7816 status word from an APDU response body: sets
 * *sw (0x9000 = OK) and shrinks *len by 2. Returns -1 if fewer than 2 bytes. */
int aliro_apdu_strip_sw(const uint8_t *buf, size_t *len, uint16_t *sw);

struct aliro_auth0_response {
	uint8_t device_eph_pub[65]; /* tag 0x86, mandatory */
	int have_cryptogram;        /* tag 0x9D present */
	uint8_t cryptogram[64];
};
int aliro_apdu_parse_auth0_response(const uint8_t *buf, size_t len,
				    // Response payload for an Aliro AUTH0 APDU exchange.
				    struct aliro_auth0_response *r);

struct aliro_auth1_response {
	int have_device_pub; /* tag 0x5A */
	uint8_t device_pub[65];
	uint8_t device_sig[64]; /* tag 0x9E, mandatory */
	uint16_t signaling;     /* 2-byte signaling bitmap */
};
int aliro_apdu_parse_auth1_response(const uint8_t *buf, size_t len,
				    // Response payload for an Aliro AUTH1 APDU exchange.
				    struct aliro_auth1_response *r);

/* ---- 4-byte L2CAP envelope: [type&0x3F][opcode][len_be16][payload] ---- */
#define ALIRO_ENVELOPE_HDR 4u
int aliro_ble_frame(uint8_t type, uint8_t opcode, const uint8_t *payload, size_t plen, uint8_t *out,
		    size_t cap, size_t *out_len);
int aliro_ble_unframe(const uint8_t *buf, size_t len, uint8_t *type, uint8_t *opcode,
		      const uint8_t **payload, size_t *plen);

#ifdef __cplusplus
}
#endif
