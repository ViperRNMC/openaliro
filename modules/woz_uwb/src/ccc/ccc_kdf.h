/**
 * @file ccc_kdf.h
 * @brief UWB ranging key schedule + SP0 frame crypto (CONFIG_WOZ_ALIRO).
 *
 * Turns the 32-byte URSK into the per-ranging-cycle keys the DW3000 STS engine
 * and the SP0 frames consume, over a single AES block-encrypt primitive.
 */

#ifndef CCC_KDF_H
#define CCC_KDF_H

#include <stddef.h>
#include <stdint.h>

/** @brief UWB Ranging Secret Key length, bytes (the schedule's root input). */
#define CCC_URSK_LEN            32
/** @brief `mUPSK1` length, bytes (AES-CCM\* key for the SP0 Pre-POLL). */
#define CCC_MUPSK1_LEN          16
/** @brief `mUPSK2` length, bytes (seed for the UWB-address KDF). */
#define CCC_MUPSK2_LEN          32
/** @brief `mURSK` length, bytes (ranging-key seed feeding `URSK_KT`). */
#define CCC_MURSK_LEN           32
/** @brief `SaltedHash` length, bytes (ranging-configuration integrity value). */
#define CCC_SALTED_HASH_LEN     16
/** @brief `URSK_KT` length, bytes (per-cycle key-transport intermediate). */
#define CCC_URSK_KT_LEN         32
/** @brief `dURSK` length, bytes (per-cycle STS key material). */
#define CCC_DURSK_LEN           16
/** @brief `dUDSK` length, bytes (per-cycle timestamp-frame key). */
#define CCC_DUDSK_LEN           16
/** @brief `STS-V` length, bytes (`phyHrpUwbStsV`, the DW3000 STS_IV). */
#define CCC_STS_V_LEN           16
/** @brief `UAD` length, bytes (the raw UWB-address derivation output). */
#define CCC_UAD_LEN             16
/** @brief AES-CMAC tag / one-block output length, bytes. */
#define CCC_CMAC_TAG_LEN        16
/** @brief KeySource length, bytes (`KeySourceHigh || KeySourceLow`). */
#define CCC_KEYSOURCE_LEN       4
/** @brief UWB destination short-address length, bytes. */
#define CCC_DEST_SHORT_ADDR_LEN 2
/** @brief UWB source long-address length, bytes (the SP0 nonce prefix). */
#define CCC_SRC_LONG_ADDR_LEN   8
/** @brief SP0 AES-CCM\* nonce length, bytes (src-long || frame-ctr || sec-lvl). */
#define CCC_SP0_NONCE_LEN       13
/** @brief SP0 Pre-POLL MIC length, bytes (Security Level 6 = ENC-MIC-64). */
#define CCC_SP0_MIC_LEN         8

/** @brief AES block-cipher seam: one 128-bit ECB block encrypt (out may alias in). */
int crypto_aes_ecb_encrypt(const uint8_t *key, size_t key_bits, const uint8_t in[16],
			   uint8_t out[16]);

/** @brief AES-CMAC over @p msg (msg may be NULL iff msg_len is 0). */
int ccc_aes_cmac(const uint8_t *key, size_t key_bits, const uint8_t *msg, size_t msg_len,
		 uint8_t tag[CCC_CMAC_TAG_LEN]);

/** @brief Derive mUPSK1 — the SP0 Pre-POLL AES-CCM* key. */
int ccc_derive_mupsk1(const uint8_t ursk[CCC_URSK_LEN], uint8_t out[CCC_MUPSK1_LEN]);

/** @brief Derive mUPSK2 — the seed for the UWB-address KDF. */
int ccc_derive_mupsk2(const uint8_t ursk[CCC_URSK_LEN], uint8_t out[CCC_MUPSK2_LEN]);

/** @brief Derive mURSK — the ranging-key seed feeding URSK_KT. */
int ccc_derive_mursk(const uint8_t ursk[CCC_URSK_LEN], uint8_t out[CCC_MURSK_LEN]);

/** @brief Derive SaltedHash from the serialized ranging configuration. */
int ccc_derive_salted_hash(const uint8_t ursk[CCC_URSK_LEN], const uint8_t *ranging_config,
			   size_t rc_len, uint8_t out[CCC_SALTED_HASH_LEN]);

/** @brief Derive URSK_KT — once per ranging cycle, keyed by the STS index. */
int ccc_derive_ursk_kt(const uint8_t mursk[CCC_MURSK_LEN], uint32_t sts_index,
		       uint8_t out[CCC_URSK_KT_LEN]);

/** @brief Derive dURSK — per-cycle STS key material. */
int ccc_derive_dursk(const uint8_t ursk_kt[CCC_URSK_KT_LEN],
		     const uint8_t salted_hash[CCC_SALTED_HASH_LEN], uint8_t out[CCC_DURSK_LEN]);

/** @brief Derive dUDSK — per-cycle SP0 timestamp-frame key. */
int ccc_derive_dudsk(const uint8_t ursk_kt[CCC_URSK_KT_LEN],
		     const uint8_t salted_hash[CCC_SALTED_HASH_LEN], uint8_t out[CCC_DUDSK_LEN]);

/** @brief Derive STS-V (phyHrpUwbStsV) — the per-PPDU STS IV for the DW3000 (out may alias
 * salted_hash). */
int ccc_derive_sts_v(const uint8_t salted_hash[CCC_SALTED_HASH_LEN], uint32_t sts_index,
		     uint8_t out[CCC_STS_V_LEN]);

/** @brief Derive UAD — the raw UWB-address derivation output. */
int ccc_derive_uad(const uint8_t mupsk2[CCC_MUPSK2_LEN], uint32_t sts_index0,
		   uint8_t out[CCC_UAD_LEN]);

/** @brief Split UAD into the UWB addresses (KeySource, dest short, src long). */
int ccc_uad_addresses(const uint8_t uad[CCC_UAD_LEN], uint8_t keysource[CCC_KEYSOURCE_LEN],
		      uint8_t dest_short_addr[CCC_DEST_SHORT_ADDR_LEN],
		      uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN]);

/** @brief Encrypt + authenticate an SP0 data frame (AES-CCM*, ENC-MIC-64). */
int ccc_sp0_encrypt(const uint8_t key[CCC_MUPSK1_LEN],
		    const uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN], uint32_t frame_counter,
		    const uint8_t *mhr, size_t mhr_len, const uint8_t *payload, size_t payload_len,
		    uint8_t *ciphertext_out, uint8_t mic_out[CCC_SP0_MIC_LEN]);

/** @brief Decrypt + verify an SP0 data frame; zeroes plaintext and returns -EBADMSG on MIC failure.
 */
int ccc_sp0_decrypt(const uint8_t key[CCC_MUPSK1_LEN],
		    const uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN], uint32_t frame_counter,
		    const uint8_t *mhr, size_t mhr_len, const uint8_t *ciphertext,
		    size_t ciphertext_len, const uint8_t mic[CCC_SP0_MIC_LEN],
		    uint8_t *payload_out);

#endif /* CCC_KDF_H */
