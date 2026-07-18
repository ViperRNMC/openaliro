/** @file ccc_crypto_psa.c — On-target AES-ECB block (PSA/CC312) backing the CCC key schedule. */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <psa/crypto.h>

#include "ccc_kdf.h"

/**
 * @brief Encrypt one AES-ECB block using PSA Crypto, supporting 128-bit and 256-bit keys.
 * @param key AES key buffer.
 * @param key_bits Key length in bits; must be 128 or 256.
 * @param in 16-byte input block to encrypt.
 * @param out 16-byte buffer to receive the encrypted block.
 * @return 0 on success, -EINVAL on invalid parameters, -EIO on crypto failure.
 */
int crypto_aes_ecb_encrypt(const uint8_t *key, size_t key_bits, const uint8_t in[16],
			   uint8_t out[16])
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id = PSA_KEY_ID_NULL;
	size_t out_len = 0;
	psa_status_t st;
	int rc = -EIO;

	if (key == NULL || in == NULL || out == NULL || (key_bits != 128u && key_bits != 256u)) {
		return -EINVAL;
	}

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_ECB_NO_PADDING);
	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attr, key_bits);

	if (psa_import_key(&attr, key, key_bits / 8u, &key_id) != PSA_SUCCESS) {
		return -EIO;
	}

	/* ECB has no IV, so the output is exactly one 16-byte block. */
	st = psa_cipher_encrypt(key_id, PSA_ALG_ECB_NO_PADDING, in, 16, out, 16, &out_len);
	if (st == PSA_SUCCESS && out_len == 16u) {
		rc = 0;
	}

	psa_destroy_key(key_id);
	return rc;
}
