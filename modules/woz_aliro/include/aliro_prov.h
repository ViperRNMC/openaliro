// Persistent reader provisioning storage: identity and credential trust anchors saved to and
// loaded from NVS.
// Declares aliro_prov_store for committing an identity/trust pair to NVS, and struct
// aliro_trust_store, the set of trusted credential public keys against which a presented
// credential is authenticated.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_prov — the reader provisioning seam (Phase 3.4): the reader's own
 * identity (a stable reader identifier + P-256 signing key) and the trust store
 * of credential public keys it authenticates presented credentials against.
 * NVS-backed on target, with a clearly-marked dev fallback so the credential-auth
 * transaction can be driven at bench before Phase-4 Matter provisioning writes a
 * real identity.
 *
 * Split like aliro_crypto: the (de)serialisation + dev default + trust logic is
 * portable and host-KAT'd (aliro_prov.c); the NVS load/store is target-only
 * (aliro_prov_nvs.c, not compiled on host). Provenance: clean-room; original.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALIRO_READER_ID_LEN   32u
#define ALIRO_READER_PRIV_LEN 32u
#define ALIRO_CRED_PUB_LEN    65u /* uncompressed P-256 point: 0x04 | X | Y */
#define ALIRO_TRUST_MAX       4u  /* trusted credential keys the store holds */
#define ALIRO_GRK_LEN         16u /* group resolving key (Aliro BLE-UWB adv tag) */

/*
 * The reader's provisioned identity. reader_id rides AUTH0 and both ECDSA
 * transcripts (tag 0x4D); sign_priv signs the reader-usage transcript. is_dev
 * marks the built-in bench identity, never a real deployment.
 */
struct aliro_reader_identity {
	uint8_t reader_id[ALIRO_READER_ID_LEN];
	uint8_t sign_priv[ALIRO_READER_PRIV_LEN];
	uint8_t grk[ALIRO_GRK_LEN]; /* group resolving key; all-zero if none */
	bool is_dev;
};

/*
 * Trusted credential public keys. A presented credential authenticates only if
 * its key is in here (or the store is empty and dev policy allows it). A raw-key
 * allowlist is the interim seam; real issuer-chain validation is the Phase-4
 * refinement that plugs in at aliro_prov_trust_check.
 */
struct aliro_trust_store {
	uint8_t count;
	uint8_t cred_pub[ALIRO_TRUST_MAX][ALIRO_CRED_PUB_LEN];
};

/* Serialised blob v2: magic(4) ver(1) flags(1) reader_id(32) sign_priv(32)
 * grk(16) count(1) then count * cred_pub(65). (v1 had no grk; still parsed.) */
#define ALIRO_PROV_BLOB_HDR 6u
#define ALIRO_PROV_BLOB_MAX                                                                        \
	(ALIRO_PROV_BLOB_HDR + ALIRO_READER_ID_LEN + ALIRO_READER_PRIV_LEN + ALIRO_GRK_LEN + 1u +  \
	 (size_t)ALIRO_TRUST_MAX * ALIRO_CRED_PUB_LEN)

/* ---- portable core (aliro_prov.c) --------------------------------------- */

/* Populate the built-in clearly-marked dev identity + an empty trust store. */
void aliro_prov_dev_default(struct aliro_reader_identity *id, struct aliro_trust_store *ts);

/* Serialise identity+trust to a self-describing blob. 0 + *out_len on success,
 * -1 on overflow (cap < the assembled length). */
int aliro_prov_serialize(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts,
			 uint8_t *out, size_t cap, size_t *out_len);

/* Parse a blob written by aliro_prov_serialize. 0 on success; -1 if malformed
 * (bad magic/version/length/count). Outputs are untouched on failure. */
int aliro_prov_deserialize(const uint8_t *buf, size_t len, struct aliro_reader_identity *id,
			   struct aliro_trust_store *ts);

/* Trust decision for a presented credential public key:
 *    0  trusted    (cred_pub matches a stored key)
 *    1  no-anchors (store empty; caller applies dev-open policy)
 *   -1  rejected   (store non-empty and no match). */
int aliro_prov_trust_check(const struct aliro_trust_store *ts,
			   const uint8_t cred_pub[ALIRO_CRED_PUB_LEN]);

/* Add a credential key to the store. 0 added; 1 already present (dedup); -1 full
 * or the point is not an uncompressed P-256 point (leading byte != 0x04). */
int aliro_prov_trust_add(struct aliro_trust_store *ts, const uint8_t cred_pub[ALIRO_CRED_PUB_LEN]);

/* ---- target NVS backend (aliro_prov_nvs.c) ------------------------------ */

/* Load identity+trust from NVS; on absence or a malformed blob fall back to the
 * dev default (leaving NVS untouched). Always yields a usable identity.
 *    0  a stored blob was loaded
 *    1  the dev default was used (nothing stored)
 *   -1  an NVS error occurred; the dev default was used. */
int aliro_prov_load(struct aliro_reader_identity *id, struct aliro_trust_store *ts);

/* Persist identity+trust to NVS. 0 on success, negative on an NVS error. */
int aliro_prov_store(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts);

#ifdef __cplusplus
}
#endif
