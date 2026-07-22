// Aliro crypto primitive backend implemented on Arm PSA Crypto: random generation, AES-256-GCM
// encrypt/decrypt, and NIST P-256 key generation, ECDH, and ECDSA sign/verify.
// Provides the aliro_prim_* / aliro_* primitive functions consumed by the higher-level Aliro KDF
// and secure-channel code in aliro_crypto.c; callers must call aliro_prim_init before using any
// other function in this file.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_prim backend for the ESP32 target: AES-256-GCM, P-256 ECDH/ECDSA, and
 * the CSPRNG, all on mbedTLS-PSA. Compiled only in the ESP-IDF build; the host
 * test build supplies its own double. See aliro_prim.h.
 */
#include "aliro_prim.h"

#include <string.h>

#include "psa/crypto.h"

/* Aliro secure-channel messages fit in one L2CAP SDU (<= 512 B); bound the
 * ciphertext||tag scratch used by GCM decrypt accordingly. */
#define ALIRO_AEAD_MAX 1024u

// Initialize the PSA Crypto backend.
// Must be called before any other aliro_prim_psa function. Returns 0 on success, -1 on failure.
int aliro_prim_init(void)
{
	return psa_crypto_init() == PSA_SUCCESS ? 0 : -1;
}

// Fill out with len bytes of cryptographically secure random data via PSA Crypto.
// Returns 0 on success, -1 on failure.
int aliro_random(uint8_t *out, size_t len)
{
	return psa_generate_random(out, len) == PSA_SUCCESS ? 0 : -1;
}

// Encrypt and authenticate plaintext with AES-256-GCM via PSA Crypto.
// Writes pt_len bytes of ciphertext to ct and tag_len bytes of authentication tag to tag. Returns 0
// on success, -1 if tag_len exceeds ALIRO_GCM_TAG, pt_len exceeds ALIRO_AEAD_MAX, key import fails,
// or encryption fails.
int aliro_aes256_gcm_encrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len,
			     const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len,
			     uint8_t *ct, uint8_t *tag, size_t tag_len)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t k = 0;
	uint8_t buf[ALIRO_AEAD_MAX + ALIRO_GCM_TAG];
	size_t olen = 0;
	int rc = -1;

	if (tag_len > ALIRO_GCM_TAG || pt_len > ALIRO_AEAD_MAX) {
		return -1;
	}
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, tag_len));
	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attr, 256);
	if (psa_import_key(&attr, key, 32, &k) != PSA_SUCCESS) {
		return -1;
	}
	if (psa_aead_encrypt(k, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, tag_len), nonce,
			     nonce_len, aad, aad_len, pt, pt_len, buf, sizeof(buf),
			     &olen) == PSA_SUCCESS &&
	    olen == pt_len + tag_len) {
		memcpy(ct, buf, pt_len);
		memcpy(tag, buf + pt_len, tag_len);
		rc = 0;
	}
	psa_destroy_key(k);
	return rc;
}

// Decrypt and authenticate an AES-256-GCM ciphertext via PSA Crypto.
// Writes ct_len bytes of plaintext to pt. Returns 0 on success (tag verified), -1 if tag_len
// exceeds ALIRO_GCM_TAG, ct_len exceeds ALIRO_AEAD_MAX, key import fails, or
// authentication/decryption fails.
int aliro_aes256_gcm_decrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len,
			     const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len,
			     const uint8_t *tag, size_t tag_len, uint8_t *pt)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t k = 0;
	uint8_t buf[ALIRO_AEAD_MAX + ALIRO_GCM_TAG];
	size_t olen = 0;
	int rc = -1;

	if (tag_len > ALIRO_GCM_TAG || ct_len > ALIRO_AEAD_MAX) {
		return -1;
	}
	memcpy(buf, ct, ct_len);
	memcpy(buf + ct_len, tag, tag_len);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, tag_len));
	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attr, 256);
	if (psa_import_key(&attr, key, 32, &k) != PSA_SUCCESS) {
		return -1;
	}
	if (psa_aead_decrypt(k, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, tag_len), nonce,
			     nonce_len, aad, aad_len, buf, ct_len + tag_len, pt, ct_len,
			     &olen) == PSA_SUCCESS &&
	    olen == ct_len) {
		rc = 0;
	}
	psa_destroy_key(k);
	return rc;
}

// Encrypt one AES-128-ECB block via PSA Crypto (the BLE advertisement Dynamic Tag).
// Returns 0 on success, -1 if key import or the cipher operation fails.
int aliro_aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t k = 0;
	size_t olen = 0;
	int rc = -1;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_ECB_NO_PADDING);
	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attr, 128);
	if (psa_import_key(&attr, key, 16, &k) != PSA_SUCCESS) {
		return -1;
	}
	/* ECB has no IV, so the one-shot output is exactly the 16-byte block. */
	if (psa_cipher_encrypt(k, PSA_ALG_ECB_NO_PADDING, in, 16, out, 16, &olen) == PSA_SUCCESS &&
	    olen == 16) {
		rc = 0;
	}
	psa_destroy_key(k);
	return rc;
}

// Generate a new NIST P-256 key pair via PSA Crypto.
// Writes the private scalar to priv and the uncompressed public point to pub. Returns 0 on success,
// -1 if key generation or export fails.
int aliro_ec_p256_keygen(uint8_t priv[ALIRO_P256_SCALAR], uint8_t pub[ALIRO_P256_POINT])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t k = 0;
	size_t plen = 0, publen = 0;
	int rc = -1;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_EXPORT);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDH);
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);
	if (psa_generate_key(&attr, &k) != PSA_SUCCESS) {
		return -1;
	}
	if (psa_export_key(k, priv, ALIRO_P256_SCALAR, &plen) == PSA_SUCCESS &&
	    plen == ALIRO_P256_SCALAR &&
	    psa_export_public_key(k, pub, ALIRO_P256_POINT, &publen) == PSA_SUCCESS &&
	    publen == ALIRO_P256_POINT) {
		rc = 0;
	}
	psa_destroy_key(k);
	return rc;
}

// Derive the uncompressed P-256 public point from an existing private scalar via PSA Crypto.
// Writes the public point to pub. Returns 0 on success, -1 if the private key import or public-key
// export fails.
int aliro_ec_p256_pub_from_priv(const uint8_t priv[ALIRO_P256_SCALAR],
				uint8_t pub[ALIRO_P256_POINT])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t k = 0;
	size_t publen = 0;
	int rc = -1;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_EXPORT);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDH);
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);
	if (psa_import_key(&attr, priv, ALIRO_P256_SCALAR, &k) != PSA_SUCCESS) {
		return -1;
	}
	if (psa_export_public_key(k, pub, ALIRO_P256_POINT, &publen) == PSA_SUCCESS &&
	    publen == ALIRO_P256_POINT) {
		rc = 0;
	}
	psa_destroy_key(k);
	return rc;
}

// Compute the P-256 ECDH shared secret x-coordinate for priv and a peer's public point via PSA
// Crypto. Writes the shared x-coordinate to shared_x. Returns 0 on success, -1 if key import or key
// agreement fails.
int aliro_ecdh_p256(const uint8_t priv[ALIRO_P256_SCALAR], const uint8_t peer_pub[ALIRO_P256_POINT],
		    uint8_t shared_x[ALIRO_P256_SCALAR])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t k = 0;
	size_t olen = 0;
	int rc = -1;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DERIVE);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDH);
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);
	if (psa_import_key(&attr, priv, ALIRO_P256_SCALAR, &k) != PSA_SUCCESS) {
		return -1;
	}
	if (psa_raw_key_agreement(PSA_ALG_ECDH, k, peer_pub, ALIRO_P256_POINT, shared_x,
				  ALIRO_P256_SCALAR, &olen) == PSA_SUCCESS &&
	    olen == ALIRO_P256_SCALAR) {
		rc = 0;
	}
	psa_destroy_key(k);
	return rc;
}

// Sign a message with ECDSA over P-256 using SHA-256, via PSA Crypto.
// Writes the signature to sig. Returns 0 on success, -1 if private key import or signing fails.
int aliro_ecdsa_p256_sign(const uint8_t priv[ALIRO_P256_SCALAR], const uint8_t *msg, size_t msg_len,
			  uint8_t sig[ALIRO_P256_SIG])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t k = 0;
	size_t slen = 0;
	int rc = -1;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);
	if (psa_import_key(&attr, priv, ALIRO_P256_SCALAR, &k) != PSA_SUCCESS) {
		return -1;
	}
	if (psa_sign_message(k, PSA_ALG_ECDSA(PSA_ALG_SHA_256), msg, msg_len, sig, ALIRO_P256_SIG,
			     &slen) == PSA_SUCCESS &&
	    slen == ALIRO_P256_SIG) {
		rc = 0;
	}
	psa_destroy_key(k);
	return rc;
}

// Verify an ECDSA-P256/SHA-256 signature against a message and public key, via PSA Crypto.
// Returns 0 if the signature verifies, -1 if public key import fails or verification fails.
int aliro_ecdsa_p256_verify(const uint8_t pub[ALIRO_P256_POINT], const uint8_t *msg, size_t msg_len,
			    const uint8_t sig[ALIRO_P256_SIG])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t k = 0;
	int rc = -1;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	if (psa_import_key(&attr, pub, ALIRO_P256_POINT, &k) != PSA_SUCCESS) {
		return -1;
	}
	if (psa_verify_message(k, PSA_ALG_ECDSA(PSA_ALG_SHA_256), msg, msg_len, sig,
			       ALIRO_P256_SIG) == PSA_SUCCESS) {
		rc = 0;
	}
	psa_destroy_key(k);
	return rc;
}
