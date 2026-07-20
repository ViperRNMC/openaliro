// Aliro APDU TLV codec: builds command payloads (AUTH0, AUTH1, AuthData, EXCHANGE) and parses
// response APDUs, plus BLE envelope framing/unframing and ISO7816 APDU wrap/status-word stripping.
// Provides a minimal BER-TLV writer (aliro_tlv_w_init/put/finish) used to assemble command
// payloads, and TLV/APDU parsing helpers used to extract fields from device responses.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Aliro credential-auth wire codec. See aliro_apdu.h.
 */
#include "aliro_apdu.h"

#include <string.h>

/* 4-byte usage domain separators appended to the ECDSA transcript. */
static const uint8_t k_reader_usage[4] = {0x41, 0x5D, 0x95, 0x69};
static const uint8_t k_user_device_usage[4] = {0x4E, 0x88, 0x7B, 0x4C};

/* ---- BER-TLV writer ---- */

// Initializes a TLV writer to append into buf, with capacity cap and no error latched.
// Must be called before any aliro_tlv_put / aliro_tlv_put_empty / w_byte calls on w.
void aliro_tlv_w_init(struct aliro_tlv_w *w, uint8_t *buf, size_t cap)
{
	w->buf = buf;
	w->cap = cap;
	w->len = 0;
	w->err = 0;
}

// Appends a single byte to the TLV writer's buffer.
// If the buffer is already full (len >= cap), sets w->err and writes nothing; callers must check
// w->err after a write sequence rather than after each byte.
static void w_byte(struct aliro_tlv_w *w, uint8_t b)
{
	if (w->len >= w->cap) {
		w->err = 1;
		return;
	}
	w->buf[w->len++] = b;
}

// Encode and append a BER-TLV length field to the writer: single byte if len < 0x80, 0x81 + one
// length byte if len <= 0xff, otherwise 0x82 + two big-endian length bytes.
static void w_len(struct aliro_tlv_w *w, size_t len)
{
	if (len < 0x80u) {
		w_byte(w, (uint8_t)len);
	} else if (len <= 0xffu) {
		w_byte(w, 0x81u);
		w_byte(w, (uint8_t)len);
	} else {
		w_byte(w, 0x82u);
		w_byte(w, (uint8_t)(len >> 8));
		w_byte(w, (uint8_t)len);
	}
}

// Append a complete TLV item (tag byte, encoded length, then len bytes of val) to the writer.
void aliro_tlv_put(struct aliro_tlv_w *w, uint8_t tag, const uint8_t *val, size_t len)
{
	w_byte(w, tag);
	w_len(w, len);
	for (size_t i = 0; i < len; i++) {
		w_byte(w, val[i]);
	}
}

// Append a TLV item whose value is the single byte v.
void aliro_tlv_put_u8(struct aliro_tlv_w *w, uint8_t tag, uint8_t v)
{
	aliro_tlv_put(w, tag, &v, 1);
}

// Append a TLV item whose 2-byte value is v encoded big-endian.
void aliro_tlv_put_u16(struct aliro_tlv_w *w, uint8_t tag, uint16_t v)
{
	uint8_t be[2] = {(uint8_t)(v >> 8), (uint8_t)v};

	aliro_tlv_put(w, tag, be, 2);
}

void aliro_tlv_put_empty(struct aliro_tlv_w *w, uint8_t tag)
{
	w_byte(w, tag);
	w_len(w, 0);
}

int aliro_tlv_w_finish(struct aliro_tlv_w *w, size_t *out_len)
{
	if (w->err) {
		return -1;
	}
	*out_len = w->len;
	return 0;
}

/* ---- BER-TLV reader ---- */

/* Read one item at *pos. On success advance *pos and set tag/val/len. */
static int tlv_read(const uint8_t *buf, size_t buf_len, size_t *pos, uint8_t *tag,
		    const uint8_t **val, size_t *val_len)
{
	size_t p = *pos;

	if (p + 2 > buf_len) {
		return -1;
	}
	uint8_t t = buf[p++];
	size_t len;
	uint8_t l0 = buf[p++];

	if (l0 < 0x80u) {
		len = l0;
	} else if (l0 == 0x81u) {
		if (p + 1 > buf_len) {
			return -1;
		}
		len = buf[p++];
	} else if (l0 == 0x82u) {
		if (p + 2 > buf_len) {
			return -1;
		}
		len = (size_t)buf[p] << 8 | buf[p + 1];
		p += 2;
	} else {
		return -1;
	}
	if (p + len > buf_len) {
		return -1;
	}
	*tag = t;
	*val = buf + p;
	*val_len = len;
	*pos = p + len;
	return 0;
}

int aliro_tlv_find(const uint8_t *buf, size_t buf_len, uint8_t tag, const uint8_t **val,
		   size_t *val_len)
{
	size_t pos = 0;
	uint8_t t;

	while (tlv_read(buf, buf_len, &pos, &t, val, val_len) == 0) {
		if (t == tag) {
			return 0;
		}
	}
	return -1;
}

/* ---- command builders ---- */

int aliro_apdu_build_auth0(uint8_t exp_phase, uint8_t user_policy, uint16_t version,
			   const uint8_t reader_eph_pub[65], const uint8_t txid[16],
			   const uint8_t reader_id[32], uint8_t *out, size_t cap, size_t *out_len)
{
	struct aliro_tlv_w w;

	aliro_tlv_w_init(&w, out, cap);
	/* ExpeditedPhaseType is a 1-byte value (0 = standard path), NOT a zero-length
	 * item: the reference TLVWriter::Put(Tag, uint8) always writes length 1, and
	 * nyrek's kormax/ST-sourced builder writes `41 01 00`. A zero-length mandatory
	 * field is what the phone rejected (GeneralError). */
	aliro_tlv_put_u8(&w, ALIRO_TAG_EXP_PHASE, exp_phase);
	aliro_tlv_put_u8(&w, ALIRO_TAG_USER_POL, user_policy);
	aliro_tlv_put_u16(&w, ALIRO_TAG_VERSION, version);
	aliro_tlv_put(&w, ALIRO_TAG_READER_EPH, reader_eph_pub, 65);
	aliro_tlv_put(&w, ALIRO_TAG_TXID, txid, 16);
	aliro_tlv_put(&w, ALIRO_TAG_READER_ID, reader_id, 32);
	return aliro_tlv_w_finish(&w, out_len);
}

// Build the AUTH1 command TLV: a 1-byte credential-type/exp-phase tag followed by the 64-byte
// ECDSA signature. Writes into out (capacity cap) and sets *out_len via aliro_tlv_w_finish.
// Returns whatever aliro_tlv_w_finish returns (0 on success, nonzero on writer overflow).
int aliro_apdu_build_auth1(uint8_t cred_type, const uint8_t sig[64], uint8_t *out, size_t cap,
			   size_t *out_len)
{
	struct aliro_tlv_w w;

	aliro_tlv_w_init(&w, out, cap);
	aliro_tlv_put_u8(&w, ALIRO_TAG_EXP_PHASE, cred_type);
	aliro_tlv_put(&w, ALIRO_TAG_SIG, sig, 64);
	return aliro_tlv_w_finish(&w, out_len);
}

// Build the plaintext AuthData TLV blob used as the ECDSA signing/verification input for
// AUTH0/AUTH1. Encodes reader_id, device_pubx, reader_eph_pubx, txid, and a 4-byte usage tag
// selected by which (ALIRO_AUTH_READER vs. user/device usage), in that fixed order. Writes into out
// (capacity cap) and sets *out_len via aliro_tlv_w_finish. Returns whatever aliro_tlv_w_finish
// returns (0 on success, nonzero if the writer latched an overflow error).
int aliro_apdu_build_authdata(int which, const uint8_t reader_id[32], const uint8_t device_pubx[32],
			      const uint8_t reader_eph_pubx[32], const uint8_t txid[16],
			      uint8_t *out, size_t cap, size_t *out_len)
{
	struct aliro_tlv_w w;

	aliro_tlv_w_init(&w, out, cap);
	aliro_tlv_put(&w, ALIRO_TAG_READER_ID, reader_id, 32);
	aliro_tlv_put(&w, ALIRO_TAG_DEVICE_PUBX, device_pubx, 32);
	aliro_tlv_put(&w, ALIRO_TAG_READER_EPH, reader_eph_pubx, 32);
	aliro_tlv_put(&w, ALIRO_TAG_TXID, txid, 16);
	aliro_tlv_put(&w, ALIRO_TAG_USAGE,
		      which == ALIRO_AUTH_READER ? k_reader_usage : k_user_device_usage, 4);
	return aliro_tlv_w_finish(&w, out_len);
}

int aliro_apdu_build_exchange(int have_status, uint16_t reader_status, int ursk_ready, uint8_t *out,
			      size_t cap, size_t *out_len)
{
	struct aliro_tlv_w w;

	aliro_tlv_w_init(&w, out, cap);
	if (have_status) {
		aliro_tlv_put_u16(&w, ALIRO_TAG_STATUS, reader_status);
	}
	if (ursk_ready) {
		aliro_tlv_put_empty(&w, ALIRO_TAG_URSK_READY); /* 98 00 */
	}
	return aliro_tlv_w_finish(&w, out_len);
}

int aliro_apdu_wrap(uint8_t ins, const uint8_t *tlv, size_t tlv_len, uint8_t *out, size_t cap,
		    size_t *out_len)
{
	/* ISO7816 case-4 short form: CLA INS P1 P2 Lc <data> Le. Lc is one byte, so
	 * the command data must fit in 255; Le = 0x00 requests up to 256 back. */
	if (tlv == NULL || tlv_len == 0 || tlv_len > 0xffu || cap < 6u + tlv_len) {
		return -1;
	}
	out[0] = 0x80u;            /* CLA: proprietary */
	out[1] = ins;              /* INS: AUTH0 / AUTH1 / EXCHANGE */
	out[2] = 0x00u;            /* P1 */
	out[3] = 0x00u;            /* P2 */
	out[4] = (uint8_t)tlv_len; /* Lc */
	memcpy(out + 5, tlv, tlv_len);
	out[5 + tlv_len] = 0x00u; /* Le (0 => 256) */
	*out_len = 6u + tlv_len;
	return 0;
}

/* ---- response parsers ---- */

/* Drop the trailing ISO7816 status word (SW1 SW2) from an APDU response body.
 * Returns the SW via *sw (0x9000 = OK) and shortens *len, or -1 if too short. */
int aliro_apdu_strip_sw(const uint8_t *buf, size_t *len, uint16_t *sw)
{
	if (buf == NULL || len == NULL || *len < 2) {
		return -1;
	}
	*len -= 2;
	if (sw != NULL) {
		*sw = (uint16_t)(((uint16_t)buf[*len] << 8) | buf[*len + 1]);
	}
	return 0;
}

// Parses an AUTH0 response APDU body, extracting the device's ephemeral public key and optional
// cryptogram. buf/len is the APDU body with any status word already stripped. The device ephemeral
// public key (tag ALIRO_TAG_DEVICE_PUBX) is mandatory and must be exactly 65 bytes; the cryptogram
// (tag 0x9D) is optional and, if present, must be exactly 64 bytes. Returns 0 on success with *r
// populated (zero-initialized first); returns -1 if the mandatory pubkey TLV is missing or has the
// wrong length.
int aliro_apdu_parse_auth0_response(
	const uint8_t *buf, size_t len,
	// Holds the fields parsed from an AUTH0 response APDU: the device's mandatory ephemeral
	// public key and an optional cryptogram.
	struct aliro_auth0_response *r)
{
	const uint8_t *v;
	size_t vl;

	memset(r, 0, sizeof(*r));
	if (aliro_tlv_find(buf, len, ALIRO_TAG_DEVICE_PUBX, &v, &vl) != 0 || vl != 65) {
		return -1; /* device ephemeral pubkey is mandatory, exactly 65 */
	}
	memcpy(r->device_eph_pub, v, 65);
	if (aliro_tlv_find(buf, len, 0x9Du, &v, &vl) == 0 && vl == 64) {
		r->have_cryptogram = 1;
		memcpy(r->cryptogram, v, 64);
	}
	return 0;
}

// Parses an AUTH1 response APDU body, extracting the device's signature and optional device public
// key. buf/len is the APDU body with any status word already stripped. The device signature (tag
// ALIRO_TAG_SIG) is mandatory and must be exactly 64 bytes; the device public key (tag
// ALIRO_TAG_DEVICE_PUB) is optional and, if present, must be exactly 65 bytes. A signaling-bitmap
// item at tag 0x91 is recognized but ignored. Returns 0 on success with *r populated
// (zero-initialized first); returns -1 if the mandatory signature TLV is missing or has the wrong
// length.
int aliro_apdu_parse_auth1_response(
	const uint8_t *buf, size_t len,
	// Holds the fields parsed from an AUTH1 response APDU: the device's mandatory signature and
	// an optional device public key.
	struct aliro_auth1_response *r)
{
	const uint8_t *v;
	size_t vl;

	memset(r, 0, sizeof(*r));
	if (aliro_tlv_find(buf, len, ALIRO_TAG_SIG, &v, &vl) != 0 || vl != 64) {
		return -1; /* device signature is mandatory, exactly 64 */
	}
	memcpy(r->device_sig, v, 64);
	if (aliro_tlv_find(buf, len, ALIRO_TAG_DEVICE_PUB, &v, &vl) == 0 && vl == 65) {
		r->have_device_pub = 1;
		memcpy(r->device_pub, v, 65);
	}
	if (aliro_tlv_find(buf, len, 0x91u, &v, &vl) == 0) {
		/* signaling bitmap rides as a bare 2-byte item on some builds; the
		 * mandatory device signature above is what we gate on. */
	}
	return 0;
}

/* ---- L2CAP envelope ---- */

// Frames a payload into an Aliro BLE envelope: 1-byte type (top 2 bits masked off), 1-byte opcode,
// 2-byte big-endian payload length, followed by the payload. Returns 0 on success with *out_len set
// to the total framed length; returns -1 if plen exceeds 0xFFFF or cap is too small to hold the
// header plus payload.
int aliro_ble_frame(uint8_t type, uint8_t opcode, const uint8_t *payload, size_t plen, uint8_t *out,
		    size_t cap, size_t *out_len)
{
	if (plen > 0xffffu || cap < ALIRO_ENVELOPE_HDR + plen) {
		return -1;
	}
	out[0] = (uint8_t)(type & 0x3Fu);
	out[1] = opcode;
	out[2] = (uint8_t)(plen >> 8);
	out[3] = (uint8_t)plen;
	if (plen) {
		memcpy(out + ALIRO_ENVELOPE_HDR, payload, plen);
	}
	*out_len = ALIRO_ENVELOPE_HDR + plen;
	return 0;
}

// Parses an Aliro BLE envelope, extracting the type, opcode, and a pointer/length into the payload
// region of buf. The returned *payload points into buf; the caller must not use it beyond buf's
// lifetime. Returns 0 on success; returns -1 if len is shorter than the envelope header, or the
// encoded payload length would exceed the buffer.
int aliro_ble_unframe(const uint8_t *buf, size_t len, uint8_t *type, uint8_t *opcode,
		      const uint8_t **payload, size_t *plen)
{
	if (len < ALIRO_ENVELOPE_HDR) {
		return -1;
	}
	size_t p = (size_t)buf[2] << 8 | buf[3];

	if (p + ALIRO_ENVELOPE_HDR > len) {
		return -1;
	}
	*type = (uint8_t)(buf[0] & 0x3Fu);
	*opcode = buf[1];
	*payload = buf + ALIRO_ENVELOPE_HDR;
	*plen = p;
	return 0;
}
