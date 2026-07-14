/** @file ccc_crypto_mbedtls.c — AES-ECB block via mbedTLS, backing the CCC key schedule on SoCs without a PSA provider (e.g. ESP32-S3). */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <mbedtls/aes.h>

#include "ccc_kdf.h"

/**
 * @brief Encrypt one AES-ECB block using mbedTLS, supporting 128-bit and 256-bit keys, as the portable crypto seam selected by CONFIG_WOZ_CRYPTO_MBEDTLS (see docs/porting.md; same contract as ccc_crypto_psa.c).
 * @param key AES key buffer.
 * @param key_bits Key length in bits; must be 128 or 256.
 * @param in 16-byte input block to encrypt.
 * @param out 16-byte buffer to receive the encrypted block.
 * @return 0 on success, -EINVAL on invalid parameters, -EIO on crypto failure.
 */
int crypto_aes_ecb_encrypt(const uint8_t *key, size_t key_bits,
			   const uint8_t in[16], uint8_t out[16])
{
	mbedtls_aes_context ctx;
	int rc = -EIO;

	if (key == NULL || in == NULL || out == NULL ||
	    (key_bits != 128u && key_bits != 256u)) {
		return -EINVAL;
	}

	mbedtls_aes_init(&ctx);
	if (mbedtls_aes_setkey_enc(&ctx, key, (unsigned int)key_bits) == 0 &&
	    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out) == 0) {
		rc = 0;
	}
	mbedtls_aes_free(&ctx);

	return rc;
}
