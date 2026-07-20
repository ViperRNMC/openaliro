// Streaming SHA-256 (FIPS 180-4) implementation used by the Aliro crypto layer.
// Declares struct aliro_sha256, the incremental hash context used across init/update/finish
// calls.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_hash — portable SHA-256 and its KDF constructions (HMAC-SHA256,
 * HKDF-SHA256, ANSI-X9.63 KDF). Pure C11, no platform dependency, so the exact
 * same object is compiled on the ESP32 target and in the host known-answer
 * tests: a host KAT here is a direct proof of the on-target key schedule.
 *
 * These are the building blocks of the Aliro credential-auth key derivation
 * (see aliro_crypto.h). AES-GCM and P-256 (ECDH/ECDSA) are NOT here; those use
 * the platform crypto backend (mbedTLS-PSA on target).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALIRO_SHA256_BLOCK 64u
#define ALIRO_SHA256_LEN   32u

/* Streaming SHA-256 (FIPS 180-4). */
struct aliro_sha256 {
	uint32_t h[8];
	uint64_t total; /* message length in bytes */
	uint8_t buf[ALIRO_SHA256_BLOCK];
	size_t buflen;
};

void aliro_sha256_init(struct aliro_sha256 *s);
void aliro_sha256_update(struct aliro_sha256 *s, const void *data, size_t len);
void aliro_sha256_final(struct aliro_sha256 *s, uint8_t out[ALIRO_SHA256_LEN]);

/* One-shot SHA-256. */
void aliro_sha256(const void *data, size_t len, uint8_t out[ALIRO_SHA256_LEN]);

/* HMAC-SHA256 (RFC 2104). out is 32 bytes. */
void aliro_hmac_sha256(const uint8_t *key, size_t key_len, const void *msg,
		       size_t msg_len, uint8_t out[ALIRO_SHA256_LEN]);

/*
 * HKDF-SHA256 (RFC 5869).
 *   extract: PRK = HMAC(salt, ikm); salt==NULL uses a 32-byte zero salt.
 *   expand:  OKM = T(1)|T(2)|... truncated to out_len (<= 255*32).
 * Returns 0 on success, -1 on a bad length.
 */
void aliro_hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
			size_t ikm_len, uint8_t prk[ALIRO_SHA256_LEN]);
int aliro_hkdf_expand(const uint8_t prk[ALIRO_SHA256_LEN], const uint8_t *info,
		      size_t info_len, uint8_t *out, size_t out_len);
int aliro_hkdf(const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
	       size_t ikm_len, const uint8_t *info, size_t info_len, uint8_t *out,
	       size_t out_len);

/*
 * ANSI-X9.63 KDF (SEC1 v2 KDF2), SHA-256 variant:
 *   OKM = Hash(Z | counter_be32=1 | info) | Hash(Z | counter=2 | info) | ...
 * truncated to out_len. Returns 0 on success, -1 on a bad length.
 */
int aliro_x963_kdf(const uint8_t *z, size_t z_len, const uint8_t *info,
		   size_t info_len, uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif
