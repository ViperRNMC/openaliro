<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_prim_psa.c`

Aliro crypto primitive backend implemented on Arm PSA Crypto: random generation, AES-256-GCM
encrypt/decrypt, and NIST P-256 key generation, ECDH, and ECDSA sign/verify.
Provides the aliro_prim_* / aliro_* primitive functions consumed by the higher-level Aliro KDF
and secure-channel code in aliro_crypto.c; callers must call aliro_prim_init before using any
other function in this file.

**depends on** [`modules/woz_aliro/include/aliro_prim.h`](../modules.woz_aliro.include/aliro_prim.h.md)  ·  **discussed in** [`ports/esp32-idf/components/aliro_crypto/README.md`](../../../ports/esp32-idf/components/aliro_crypto/README.md)

## API

### `int aliro_prim_init(void)`
`modules/woz_aliro/src/aliro_prim_psa.c:26`

Initialize the PSA Crypto backend.
Must be called before any other aliro_prim_psa function. Returns 0 on success, -1 on failure.

### `int aliro_random(uint8_t *out, size_t len)`
`modules/woz_aliro/src/aliro_prim_psa.c:33`

Fill out with len bytes of cryptographically secure random data via PSA Crypto.
Returns 0 on success, -1 on failure.

### `int aliro_aes256_gcm_encrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len, const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct, uint8_t *tag, size_t tag_len)`
`modules/woz_aliro/src/aliro_prim_psa.c:42`

Encrypt and authenticate plaintext with AES-256-GCM via PSA Crypto.
Writes pt_len bytes of ciphertext to ct and tag_len bytes of authentication tag to tag. Returns 0 on
success, -1 if tag_len exceeds ALIRO_GCM_TAG, pt_len exceeds ALIRO_AEAD_MAX, key import fails, or
encryption fails.

### `int aliro_aes256_gcm_decrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len, const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t *tag, size_t tag_len, uint8_t *pt)`
`modules/woz_aliro/src/aliro_prim_psa.c:78`

Decrypt and authenticate an AES-256-GCM ciphertext via PSA Crypto.
Writes ct_len bytes of plaintext to pt. Returns 0 on success (tag verified), -1 if tag_len exceeds
ALIRO_GCM_TAG, ct_len exceeds ALIRO_AEAD_MAX, key import fails, or authentication/decryption fails.

### `int aliro_ec_p256_keygen(uint8_t priv[ALIRO_P256_SCALAR], uint8_t pub[ALIRO_P256_POINT])`
`modules/woz_aliro/src/aliro_prim_psa.c:114`

Generate a new NIST P-256 key pair via PSA Crypto.
Writes the private scalar to priv and the uncompressed public point to pub. Returns 0 on success, -1 if
key generation or export fails.

### `int aliro_ec_p256_pub_from_priv(const uint8_t priv[ALIRO_P256_SCALAR], uint8_t pub[ALIRO_P256_POINT])`
`modules/woz_aliro/src/aliro_prim_psa.c:142`

Derive the uncompressed P-256 public point from an existing private scalar via PSA Crypto.
Writes the public point to pub. Returns 0 on success, -1 if the private key import or public-key export
fails.

### `int aliro_ecdh_p256(const uint8_t priv[ALIRO_P256_SCALAR], const uint8_t peer_pub[ALIRO_P256_POINT], uint8_t shared_x[ALIRO_P256_SCALAR])`
`modules/woz_aliro/src/aliro_prim_psa.c:168`

Compute the P-256 ECDH shared secret x-coordinate for priv and a peer's public point via PSA Crypto.
Writes the shared x-coordinate to shared_x. Returns 0 on success, -1 if key import or key agreement
fails.

### `int aliro_ecdsa_p256_sign(const uint8_t priv[ALIRO_P256_SCALAR], const uint8_t *msg, size_t msg_len, uint8_t sig[ALIRO_P256_SIG])`
`modules/woz_aliro/src/aliro_prim_psa.c:195`

Sign a message with ECDSA over P-256 using SHA-256, via PSA Crypto.
Writes the signature to sig. Returns 0 on success, -1 if private key import or signing fails.

### `int aliro_ecdsa_p256_verify(const uint8_t pub[ALIRO_P256_POINT], const uint8_t *msg, size_t msg_len, const uint8_t sig[ALIRO_P256_SIG])`
`modules/woz_aliro/src/aliro_prim_psa.c:222`

Verify an ECDSA-P256/SHA-256 signature against a message and public key, via PSA Crypto.
Returns 0 if the signature verifies, -1 if public key import fails or verification fails.
