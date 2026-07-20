// Aliro reader provisioning state: default dev identity, and serialization/deserialization of the
// reader identity plus trusted-credential store to/from a self-describing binary blob.
// Also implements the trust-store membership check and add-with-dedup operations used to decide
// whether a presented credential public key is trusted.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_prov (portable core) — dev identity, blob (de)serialisation, and the
 * trust-store logic. No ESP-IDF or crypto dependency, so it compiles identically
 * on host and target and is host-KAT'd (test/test_aliro_prov.c). The NVS-backed
 * load/store lives in aliro_prov_nvs.c (target only).
 */
#include <string.h>

#include "aliro_prov.h"

/*
 * Built-in DEV reader identity — a fixed, non-secret P-256 bench keypair so the
 * reader identity is stable across reboots (a phone provisioned against it stays
 * valid). NOT for deployment: Phase-4 Matter provisioning overwrites this in NVS.
 * reader_id = the signing public key's X coordinate (the dev convention the
 * earlier scaffold used); sign_priv = the matching private scalar.
 */
static const uint8_t k_dev_reader_id[ALIRO_READER_ID_LEN] = {
	0x11, 0x3b, 0x1a, 0x9e, 0xf2, 0x95, 0x67, 0x60,
	0x8b, 0x75, 0x00, 0xfb, 0xac, 0xa6, 0x09, 0xe9,
	0xc0, 0x7b, 0x87, 0x4e, 0x18, 0x2a, 0xe5, 0x65,
	0x02, 0x4b, 0x54, 0x3e, 0x3b, 0x40, 0x93, 0x5f,
};
static const uint8_t k_dev_sign_priv[ALIRO_READER_PRIV_LEN] = {
	0x4d, 0x33, 0x21, 0x69, 0xf4, 0x33, 0x9e, 0xef,
	0x54, 0x9e, 0xf2, 0xa6, 0xa9, 0x4b, 0x61, 0x95,
	0xa4, 0x2f, 0xc2, 0xaf, 0x8c, 0xcf, 0xdf, 0xce,
	0x35, 0xbd, 0xf9, 0xbe, 0xd8, 0xf3, 0x83, 0xa3,
};

static const uint8_t k_magic[4] = { 'A', 'P', 'R', 'V' };
#define ALIRO_PROV_VERSION   0x02u /* current: includes grk(16) */
#define ALIRO_PROV_VERSION_1 0x01u /* legacy: no grk (still parsed) */
#define ALIRO_PROV_FLAG_DEV  0x01u

void aliro_prov_dev_default(struct aliro_reader_identity *id,
			    struct aliro_trust_store *ts)
{
	if (id != NULL) {
		memset(id, 0, sizeof(*id)); /* zeroes grk (dev has none) + padding */
		memcpy(id->reader_id, k_dev_reader_id, ALIRO_READER_ID_LEN);
		memcpy(id->sign_priv, k_dev_sign_priv, ALIRO_READER_PRIV_LEN);
		id->is_dev = true;
	}
	if (ts != NULL) {
		memset(ts, 0, sizeof(*ts));
	}
}

int aliro_prov_serialize(const struct aliro_reader_identity *id,
			 const struct aliro_trust_store *ts,
			 uint8_t *out, size_t cap, size_t *out_len)
{
	uint8_t count = (ts != NULL) ? ts->count : 0u;

	if (count > ALIRO_TRUST_MAX) {
		return -1;
	}

	size_t need = ALIRO_PROV_BLOB_HDR + ALIRO_READER_ID_LEN +
		      ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN + 1u +
		      (size_t)count * ALIRO_CRED_PUB_LEN;

	if (out == NULL || cap < need) {
		return -1;
	}

	uint8_t *p = out;

	memcpy(p, k_magic, sizeof(k_magic));
	p += sizeof(k_magic);
	*p++ = ALIRO_PROV_VERSION;
	*p++ = id->is_dev ? ALIRO_PROV_FLAG_DEV : 0u;
	memcpy(p, id->reader_id, ALIRO_READER_ID_LEN);
	p += ALIRO_READER_ID_LEN;
	memcpy(p, id->sign_priv, ALIRO_READER_PRIV_LEN);
	p += ALIRO_READER_PRIV_LEN;
	memcpy(p, id->grk, ALIRO_GRK_LEN);
	p += ALIRO_GRK_LEN;
	*p++ = count;
	for (uint8_t i = 0; i < count; i++) {
		memcpy(p, ts->cred_pub[i], ALIRO_CRED_PUB_LEN);
		p += ALIRO_CRED_PUB_LEN;
	}

	if (out_len != NULL) {
		*out_len = (size_t)(p - out);
	}
	return 0;
}

int aliro_prov_deserialize(const uint8_t *buf, size_t len,
			   struct aliro_reader_identity *id,
			   struct aliro_trust_store *ts)
{
	if (buf == NULL || len < ALIRO_PROV_BLOB_HDR ||
	    memcmp(buf, k_magic, sizeof(k_magic)) != 0) {
		return -1;
	}

	/* grk was added in v2; v1 blobs (no grk) are still parsed for back-compat. */
	size_t grk_len;
	if (buf[4] == ALIRO_PROV_VERSION) {
		grk_len = ALIRO_GRK_LEN;
	} else if (buf[4] == ALIRO_PROV_VERSION_1) {
		grk_len = 0u;
	} else {
		return -1;
	}

	const size_t fixed = ALIRO_PROV_BLOB_HDR + ALIRO_READER_ID_LEN +
			     ALIRO_READER_PRIV_LEN + grk_len + 1u;

	if (len < fixed) {
		return -1;
	}

	uint8_t count = buf[fixed - 1u];

	if (count > ALIRO_TRUST_MAX ||
	    len != fixed + (size_t)count * ALIRO_CRED_PUB_LEN) {
		return -1;
	}

	const uint8_t *p = buf + ALIRO_PROV_BLOB_HDR;

	if (id != NULL) {
		id->is_dev = (buf[5] & ALIRO_PROV_FLAG_DEV) != 0u;
		memcpy(id->reader_id, p, ALIRO_READER_ID_LEN);
		memcpy(id->sign_priv, p + ALIRO_READER_ID_LEN, ALIRO_READER_PRIV_LEN);
		if (grk_len == ALIRO_GRK_LEN) {
			memcpy(id->grk, p + ALIRO_READER_ID_LEN + ALIRO_READER_PRIV_LEN,
			       ALIRO_GRK_LEN);
		} else {
			memset(id->grk, 0, ALIRO_GRK_LEN);
		}
	}
	if (ts != NULL) {
		memset(ts, 0, sizeof(*ts));
		ts->count = count;
		const uint8_t *k = buf + fixed;

		for (uint8_t i = 0; i < count; i++) {
			memcpy(ts->cred_pub[i], k, ALIRO_CRED_PUB_LEN);
			k += ALIRO_CRED_PUB_LEN;
		}
	}
	return 0;
}

int aliro_prov_trust_check(const struct aliro_trust_store *ts,
			   const uint8_t cred_pub[ALIRO_CRED_PUB_LEN])
{
	if (ts == NULL || ts->count == 0u) {
		return 1; /* no anchors provisioned */
	}
	for (uint8_t i = 0; i < ts->count && i < ALIRO_TRUST_MAX; i++) {
		if (memcmp(ts->cred_pub[i], cred_pub, ALIRO_CRED_PUB_LEN) == 0) {
			return 0; /* trusted */
		}
	}
	return -1; /* known set, not a member */
}

int aliro_prov_trust_add(struct aliro_trust_store *ts,
			 const uint8_t cred_pub[ALIRO_CRED_PUB_LEN])
{
	if (ts == NULL || cred_pub[0] != 0x04u) {
		return -1;
	}
	if (aliro_prov_trust_check(ts, cred_pub) == 0) {
		return 1; /* already present */
	}
	if (ts->count >= ALIRO_TRUST_MAX) {
		return -1; /* full */
	}
	memcpy(ts->cred_pub[ts->count], cred_pub, ALIRO_CRED_PUB_LEN);
	ts->count++;
	return 0;
}
