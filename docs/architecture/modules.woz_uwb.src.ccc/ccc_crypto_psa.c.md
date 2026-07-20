<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/ccc/ccc_crypto_psa.c`

@file ccc_crypto_psa.c — On-target AES-ECB block (PSA/CC312) backing the CCC key schedule.

**depends on** [`modules/woz_uwb/src/ccc/ccc_kdf.h`](ccc_kdf.h.md)

## API

### `int crypto_aes_ecb_encrypt(const uint8_t *key, size_t key_bits, const uint8_t in[16], uint8_t out[16])`
`modules/woz_uwb/src/ccc/ccc_crypto_psa.c:19`

@brief Encrypt one AES-ECB block using PSA Crypto, supporting 128-bit and 256-bit keys.
@param key AES key buffer.
@param key_bits Key length in bits; must be 128 or 256.
@param in 16-byte input block to encrypt.
@param out 16-byte buffer to receive the encrypted block.
@return 0 on success, -EINVAL on invalid parameters, -EIO on crypto failure.
