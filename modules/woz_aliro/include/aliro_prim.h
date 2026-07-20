/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_prim — the AEAD + elliptic-curve + RNG primitive interface used by the
 * Aliro credential-auth composition (aliro_crypto.c). Two backends implement it:
 *   - aliro_prim_psa.c   on the ESP32 target (mbedTLS-PSA)
 *   - a host double in the test build (for the secure-channel nonce/AAD tests)
 *
 * Hashing/KDF is NOT here; that is the portable aliro_hash.c, shared by both.
 * All returns: 0 on success, negative on failure (AEAD decrypt returns <0 on a
 * tag mismatch and must be treated as a hard auth failure).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALIRO_P256_SCALAR 32u /* private scalar / coordinate */
#define ALIRO_P256_POINT  65u /* uncompressed point: 0x04 | X32 | Y32 */
#define ALIRO_P256_SIG    64u /* raw ECDSA r|s */
#define ALIRO_GCM_TAG     16u

/* Initialise the backend (idempotent). Call once before any other call. */
int aliro_prim_init(void);

/* CSPRNG. */
int aliro_random(uint8_t *out, size_t len);

/* AES-256-GCM. tag_len must be <= 16. Decrypt verifies the tag. */
int aliro_aes256_gcm_encrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len,
			     const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len,
			     uint8_t *ct, uint8_t *tag, size_t tag_len);
int aliro_aes256_gcm_decrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len,
			     const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len,
			     const uint8_t *tag, size_t tag_len, uint8_t *pt);

/* P-256 ephemeral key pair: priv = 32-byte scalar, pub = 65-byte point. */
int aliro_ec_p256_keygen(uint8_t priv[ALIRO_P256_SCALAR], uint8_t pub[ALIRO_P256_POINT]);

/* Derive the 65-byte uncompressed public key from a 32-byte P-256 private
 * scalar (used to recover the reader group key X from the provisioned
 * signingKey; verificationKey = pub(signingKey)). */
int aliro_ec_p256_pub_from_priv(const uint8_t priv[ALIRO_P256_SCALAR],
				uint8_t pub[ALIRO_P256_POINT]);

/* ECDH: shared_x = X coordinate (32 bytes) of priv * peer_pub. */
int aliro_ecdh_p256(const uint8_t priv[ALIRO_P256_SCALAR], const uint8_t peer_pub[ALIRO_P256_POINT],
		    uint8_t shared_x[ALIRO_P256_SCALAR]);

/* ECDSA-P256-SHA256 over the raw message (hashing is internal). sig = r|s. */
int aliro_ecdsa_p256_sign(const uint8_t priv[ALIRO_P256_SCALAR], const uint8_t *msg, size_t msg_len,
			  uint8_t sig[ALIRO_P256_SIG]);
int aliro_ecdsa_p256_verify(const uint8_t pub[ALIRO_P256_POINT], const uint8_t *msg, size_t msg_len,
			    const uint8_t sig[ALIRO_P256_SIG]);

#ifdef __cplusplus
}
#endif
