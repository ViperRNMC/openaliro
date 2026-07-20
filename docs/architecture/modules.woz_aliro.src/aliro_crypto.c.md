<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_crypto.c`

Aliro cryptographic primitives: key derivation (KDF/HKDF), key-block splitting, AES-GCM secure
channels, and wire message framing built on a pluggable crypto backend (aliro_prim_*).
Implements the Aliro key-derivation chain (ECDH shared secret -> z -> 160-byte key block -> split
session keys / URSK / BLE ranging keys), per-direction AES-256-GCM secure channels with monotonic
message counters, and the seal/open framing used to carry engine plaintext over the wire.

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](../modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_prim.h`](../modules.woz_aliro.include/aliro_prim.h.md), [`modules/woz_aliro/src/aliro_hash.h`](aliro_hash.h.md)  ·  **discussed in** [`ports/esp32-idf/components/aliro_crypto/README.md`](../../../ports/esp32-idf/components/aliro_crypto/README.md)

## API

### `int aliro_crypto_init(void)`
`modules/woz_aliro/src/aliro_crypto.c:26`

Initializes the Aliro crypto backend's underlying primitive library.
Must be called once before any other aliro_crypto function. Returns the underlying aliro_prim_init() status code.

### `void aliro_crypto_ursk_from_block(const uint8_t block[ALIRO_KEY_BLOCK_LEN], uint8_t ursk[ALIRO_URSK_LEN])`
`modules/woz_aliro/src/aliro_crypto.c:33`

Extracts the URSK from a derived 160-byte key block at its fixed offset.
Does not perform any derivation; block must already be the HKDF output of aliro_crypto_derive_block.

### `void aliro_crypto_derive_z(const uint8_t shared_secret[ALIRO_SHARED_SECRET_LEN], const uint8_t txid[ALIRO_TXID_LEN], uint8_t z[32])`
`modules/woz_aliro/src/aliro_crypto.c:43`

Derives the 32-byte KDF intermediate z from the ECDH shared secret and transaction ID via single-block ANSI X9.63 KDF: SHA-256(shared_secret || 00000001 || txid).
z is the input consumed by aliro_crypto_derive_block and aliro_crypto_derive_key32.

### `int aliro_crypto_derive_block(const uint8_t z[32], const uint8_t *salt, size_t salt_len, const uint8_t device_pub_x[ALIRO_EC_PUBX_LEN], uint8_t block[ALIRO_KEY_BLOCK_LEN])`
`modules/woz_aliro/src/aliro_crypto.c:54`

Derives the 160-byte Aliro key block via HKDF-SHA256(salt, IKM=z, info=device_pub_x, L=160).
z is the 32-byte KDF intermediate (aliro_crypto_derive_z output); device_pub_x is the peer device's ephemeral public-key X coordinate.
Returns 0 on success, nonzero on HKDF failure.

### `int aliro_crypto_derive_key32(const uint8_t z[32], const uint8_t *salt, size_t salt_len, const uint8_t device_pub_x[ALIRO_EC_PUBX_LEN], uint8_t out[32])`
`modules/woz_aliro/src/aliro_crypto.c:66`

Derives a standalone 32-byte key via HKDF-SHA256(salt, IKM=z, info=device_pub_x, L=32), independent of the standard 160-byte key block layout.
Returns 0 on success, nonzero on HKDF failure.

### `void aliro_crypto_split(const uint8_t block[ALIRO_KEY_BLOCK_LEN], int with_c, uint8_t enc_key[ALIRO_SESSION_KEY_LEN], uint8_t dec_key[ALIRO_SESSION_KEY_LEN], uint8_t ursk[ALIRO_URSK_LEN])`
`modules/woz_aliro/src/aliro_crypto.c:77`

Splits a derived 160-byte key block into the directional session keys and URSK.
with_c selects which pair of 32-byte segments become enc_key/dec_key: nonzero uses segments S0/S1 (base offset 0), zero uses S1/S2 (base offset 32); this corresponds to the "derive shared key C" config flag. ursk is always taken from the fixed S4 segment (offset 128) regardless of with_c.

### `void aliro_secchan_init(struct aliro_secchan *sc, const uint8_t enc_key[ALIRO_SESSION_KEY_LEN], const uint8_t dec_key[ALIRO_SESSION_KEY_LEN])`
`modules/woz_aliro/src/aliro_crypto.c:96`

Initializes a secure channel with the given per-direction encrypt/decrypt keys and sets both message counters to 1.
Per Aliro §8.3.1 both enc_ctr and dec_ctr start at 1, not 0 (unlike the HomeKey channel), so the first inbound decrypt must use device_counter=1.

### `void aliro_crypto_gcm_nonce(uint64_t direction, uint32_t counter, uint8_t nonce[ALIRO_GCM_NONCE_LEN])`
`modules/woz_aliro/src/aliro_crypto.c:110`

Builds a 12-byte AES-GCM nonce as an 8-byte big-endian direction value followed by a 4-byte big-endian counter.
direction distinguishes the two traffic directions of a secure channel; counter is the per-message sequence number that must be incremented by the caller between messages to avoid nonce reuse.

**called by** `aliro_secchan_open`, `aliro_secchan_seal`

### `int aliro_secchan_seal(struct aliro_secchan *sc, const uint8_t *aad, size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *ct, uint8_t tag[ALIRO_GCM_TAG_LEN])`
`modules/woz_aliro/src/aliro_crypto.c:127`

Encrypt and authenticate plaintext with the secure channel's outbound (enc) key and counter.
Builds the nonce from direction 0 and the current enc_ctr, writes ciphertext to ct and the GCM tag
to tag, and advances enc_ctr only on success. Returns -1 without encrypting if enc_ctr has reached
0xffffffff (counter exhausted, no wraparound), or if encryption fails; returns 0 on success.

**called by** `aliro_msg_seal`  ·  **calls** `aliro_crypto_gcm_nonce`

### `int aliro_secchan_open(struct aliro_secchan *sc, const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t tag[ALIRO_GCM_TAG_LEN], uint8_t *pt)`
`modules/woz_aliro/src/aliro_crypto.c:152`

Decrypt and authenticate a ciphertext with the secure channel's inbound (dec) key and counter.
Builds the nonce from direction 1 and the current dec_ctr, verifies tag against aad/ct, and writes
the plaintext to pt on success. Advances dec_ctr only on successful authentication. Returns -1
without decrypting if dec_ctr has reached 0xffffffff (counter exhausted, no wraparound), or if
decryption/authentication fails; returns 0 on success.

**called by** `aliro_msg_open`  ·  **calls** `aliro_crypto_gcm_nonce`

### `int aliro_crypto_derive_ble_keys(const uint8_t block[ALIRO_KEY_BLOCK_LEN], const uint8_t *salt, size_t salt_len, uint8_t ble_reader[ALIRO_SESSION_KEY_LEN], uint8_t ble_device[ALIRO_SESSION_KEY_LEN])`
`modules/woz_aliro/src/aliro_crypto.c:177`

Derives the two directional BLE ranging-channel session keys (BleSKReader, BleSKDevice) from the BleSK segment of a derived key block via HKDF.
salt/salt_len is the caller-built ranging-channel salt (protocol versions + selected version); ble_sk is read from a fixed offset within block.
Returns 0 on success, or the first nonzero HKDF return code (ble_reader is left populated even if the second derivation for ble_device fails).

### `int aliro_msg_seal(struct aliro_secchan *sc, const uint8_t *plain, size_t plain_len, uint8_t *wire, size_t wire_cap, size_t *wire_len)`
`modules/woz_aliro/src/aliro_crypto.c:201`

Seal an engine-plaintext message (4-byte header + payload) into a framed, encrypted wire SDU.
The header's 2-byte big-endian length field (plain[2..3]) must equal the payload length
(plain_len - 4); mismatches are rejected. Copies the header's first two bytes unchanged into the
wire frame, rewrites the length field as payload_len + GCM tag length, then seals the payload
using the original 4-byte plaintext header as AAD. Requires plain_len >= 4 and wire_cap large
enough for the framed output (4 + payload_len + GCM tag length); returns -1 on either violation or
if the underlying seal fails, 0 on success with *wire_len set.

**calls** `aliro_secchan_seal`

### `int aliro_msg_open(struct aliro_secchan *sc, const uint8_t *wire, size_t wire_len, uint8_t *plain, size_t plain_cap, size_t *plain_len)`
`modules/woz_aliro/src/aliro_crypto.c:233`

Represents a secure Aliro communication channel: a pair of AES-GCM session keys and per-direction message counters, used to seal/open messages exchanged with the device.

### `int aliro_msg_open(struct aliro_secchan *sc, const uint8_t *wire, size_t wire_len, uint8_t *plain, size_t plain_cap, size_t *plain_len)`
`modules/woz_aliro/src/aliro_crypto.c:233`

Represents a secure Aliro communication channel: a pair of AES-GCM session keys and per-direction message counters, used to seal/open messages exchanged with the device.

**calls** `aliro_secchan_open`

### `static int append(uint8_t *out, size_t *pos, size_t cap, const void *src, size_t n)`
`modules/woz_aliro/src/aliro_crypto.c:272`

Appends n bytes from src into out at *pos, bounds-checked against cap.
Returns 0 on success and advances *pos by n; returns -1 without writing or advancing *pos if the append would exceed cap.

**called by** `aliro_salt_build`

### `int aliro_salt_build(enum aliro_salt_type type, const uint8_t txid[ALIRO_TXID_LEN], const uint8_t span_s1[ALIRO_EC_PUBX_LEN], const uint8_t reader_value[ALIRO_EC_PUBX_LEN], const uint8_t reader_id[32], uint8_t interface_byte, uint16_t proto_version, uint8_t exp_phase_type, uint8_t user_auth_policy, const uint8_t s3opt[ALIRO_EC_PUBX_LEN], const uint8_t *a5_tlv, size_t a5_tlv_len, uint8_t *out,`
`modules/woz_aliro/src/aliro_crypto.c:289`

Build the Aliro §8.3.1.13 salt_volatile byte string for HKDF derivation from its component fields.
Appends, in fixed order: span_s1, a type-specific 12-byte label, reader_id, the interface byte
(0xC3 BLE / 0x5E NFC), a fixed salt constant, the big-endian protocol version, reader_value, txid,
and the (exp_phase_type, user_auth_policy) flag pair. If a5_tlv is non-null and non-empty, appends
the 0xA5 SELECT-response proprietary-information TLV. If type is not ALIRO_SALT_SESSION and s3opt
is non-null, appends s3opt last. Writes into out (capacity ALIRO_SALT_MAX) and sets *out_len.
Returns 0 on success, -1 if any append exceeds ALIRO_SALT_MAX.

**calls** `append`
