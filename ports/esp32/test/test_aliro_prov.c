/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host KAT for the reader provisioning core (aliro_prov.c): the dev identity,
 * blob (de)serialisation round-trip + malformed-blob rejection, and the trust
 * store (check / add / dedup / full / bad-point). Pure host build; the NVS
 * backend (aliro_prov_nvs.c) is target-only and not linked here.
 */
#include <stdio.h>
#include <string.h>

#include "aliro_prov.h"

static int fails;

static void okc(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

/* An uncompressed-point-shaped key filled from a seed byte. */
static void mkpub(uint8_t pub[ALIRO_CRED_PUB_LEN], uint8_t seed)
{
	pub[0] = 0x04;
	for (unsigned i = 1; i < ALIRO_CRED_PUB_LEN; i++) {
		pub[i] = (uint8_t)(seed + i);
	}
}

int main(void)
{
	struct aliro_reader_identity id, id2;
	struct aliro_trust_store ts, ts2;

	printf("== dev default ==\n");
	aliro_prov_dev_default(&id, &ts);
	okc("dev.is_dev", id.is_dev);
	okc("dev.trust_empty", ts.count == 0);
	/* reader_id = dev signing pub X (first bytes 11 3b 1a 9e ...). */
	okc("dev.reader_id0", id.reader_id[0] == 0x11 && id.reader_id[1] == 0x3b &&
			      id.reader_id[2] == 0x1a && id.reader_id[3] == 0x9e);
	okc("dev.sign_priv0", id.sign_priv[0] == 0x4d && id.sign_priv[1] == 0x33);
	/* dev identity is stable: two loads are byte-identical. */
	aliro_prov_dev_default(&id2, &ts2);
	okc("dev.stable", memcmp(&id, &id2, sizeof(id)) == 0);

	printf("\n== serialize / deserialize round-trip ==\n");
	uint8_t blob[ALIRO_PROV_BLOB_MAX];
	size_t n = 0;

	/* A provisioned (non-dev) identity + a 2-key trust store. */
	for (unsigned i = 0; i < ALIRO_READER_ID_LEN; i++) {
		id.reader_id[i] = (uint8_t)(0xA0 + i);
	}
	for (unsigned i = 0; i < ALIRO_READER_PRIV_LEN; i++) {
		id.sign_priv[i] = (uint8_t)(0x50 + i);
	}
	for (unsigned i = 0; i < ALIRO_GRK_LEN; i++) {
		id.grk[i] = (uint8_t)(0x30 + i);
	}
	id.is_dev = false;
	memset(&ts, 0, sizeof(ts));
	uint8_t k0[ALIRO_CRED_PUB_LEN], k1[ALIRO_CRED_PUB_LEN];

	mkpub(k0, 0x10);
	mkpub(k1, 0x60);
	okc("add.k0", aliro_prov_trust_add(&ts, k0) == 0);
	okc("add.k1", aliro_prov_trust_add(&ts, k1) == 0);
	okc("trust.count2", ts.count == 2);

	okc("ser.ok", aliro_prov_serialize(&id, &ts, blob, sizeof(blob), &n) == 0);
	okc("ser.len", n == ALIRO_PROV_BLOB_HDR + ALIRO_READER_ID_LEN +
			   ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN + 1u +
			   2u * ALIRO_CRED_PUB_LEN);
	okc("ser.magic", blob[0] == 'A' && blob[1] == 'P' && blob[2] == 'R' &&
			 blob[3] == 'V');

	okc("de.ok", aliro_prov_deserialize(blob, n, &id2, &ts2) == 0);
	okc("de.is_dev", id2.is_dev == false);
	okc("de.reader_id", memcmp(id2.reader_id, id.reader_id, ALIRO_READER_ID_LEN) == 0);
	okc("de.sign_priv", memcmp(id2.sign_priv, id.sign_priv, ALIRO_READER_PRIV_LEN) == 0);
	okc("de.grk", memcmp(id2.grk, id.grk, ALIRO_GRK_LEN) == 0);
	okc("de.count", ts2.count == 2);
	okc("de.k0", memcmp(ts2.cred_pub[0], k0, ALIRO_CRED_PUB_LEN) == 0);
	okc("de.k1", memcmp(ts2.cred_pub[1], k1, ALIRO_CRED_PUB_LEN) == 0);

	/* is_dev flag survives the round-trip. */
	aliro_prov_dev_default(&id, &ts);
	okc("ser.dev.ok", aliro_prov_serialize(&id, &ts, blob, sizeof(blob), &n) == 0);
	okc("ser.dev.len", n == ALIRO_PROV_BLOB_HDR + ALIRO_READER_ID_LEN +
			       ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN + 1u);
	okc("de.dev.ok", aliro_prov_deserialize(blob, n, &id2, &ts2) == 0);
	okc("de.dev.flag", id2.is_dev == true);
	okc("de.dev.count", ts2.count == 0);

	printf("\n== serialize overflow ==\n");
	okc("ser.overflow", aliro_prov_serialize(&id, &ts, blob, 10, &n) == -1);

	printf("\n== malformed-blob rejection ==\n");
	/* Rebuild a valid 1-key blob, then corrupt copies of it. */
	memset(&ts, 0, sizeof(ts));
	aliro_prov_trust_add(&ts, k0);
	aliro_prov_dev_default(&id, NULL);
	aliro_prov_serialize(&id, &ts, blob, sizeof(blob), &n);

	uint8_t bad[ALIRO_PROV_BLOB_MAX];

	okc("de.tooshort", aliro_prov_deserialize(blob, ALIRO_PROV_BLOB_HDR + 1, &id2, &ts2) == -1);

	memcpy(bad, blob, n);
	bad[0] = 'X';
	okc("de.badmagic", aliro_prov_deserialize(bad, n, &id2, &ts2) == -1);

	memcpy(bad, blob, n);
	bad[4] = 0xFF; /* unknown version (0x01/0x02 are valid) */
	okc("de.badver", aliro_prov_deserialize(bad, n, &id2, &ts2) == -1);

	memcpy(bad, blob, n);
	bad[ALIRO_PROV_BLOB_HDR + ALIRO_READER_ID_LEN + ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN] =
		ALIRO_TRUST_MAX + 1;
	okc("de.badcount", aliro_prov_deserialize(bad, n, &id2, &ts2) == -1);

	/* count says 1 but the buffer is truncated by a byte. */
	okc("de.lenmismatch", aliro_prov_deserialize(blob, n - 1, &id2, &ts2) == -1);

	printf("\n== trust check / add / dedup / full ==\n");
	memset(&ts, 0, sizeof(ts));
	uint8_t kx[ALIRO_CRED_PUB_LEN];

	mkpub(kx, 0xAA);
	okc("check.empty", aliro_prov_trust_check(&ts, kx) == 1);
	okc("add.first", aliro_prov_trust_add(&ts, kx) == 0);
	okc("check.hit", aliro_prov_trust_check(&ts, kx) == 0);
	mkpub(k0, 0x01);
	okc("check.miss", aliro_prov_trust_check(&ts, k0) == -1);
	okc("add.dedup", aliro_prov_trust_add(&ts, kx) == 1);
	okc("dedup.count", ts.count == 1);

	uint8_t badpt[ALIRO_CRED_PUB_LEN];

	mkpub(badpt, 0x33);
	badpt[0] = 0x02; /* not an uncompressed point */
	okc("add.badpoint", aliro_prov_trust_add(&ts, badpt) == -1);

	/* Fill to capacity, then one more must fail. */
	memset(&ts, 0, sizeof(ts));
	for (unsigned i = 0; i < ALIRO_TRUST_MAX; i++) {
		uint8_t kk[ALIRO_CRED_PUB_LEN];

		mkpub(kk, (uint8_t)(0x70 + i * 8));
		okc("fill.add", aliro_prov_trust_add(&ts, kk) == 0);
	}
	okc("fill.count", ts.count == ALIRO_TRUST_MAX);
	uint8_t over[ALIRO_CRED_PUB_LEN];

	mkpub(over, 0xF0);
	okc("fill.overflow", aliro_prov_trust_add(&ts, over) == -1);

	printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
	return fails == 0 ? 0 : 1;
}
