/** @file ccc_kdf.c — UWB key schedule + SP0 Pre-POLL frame codec. */

#include "ccc_kdf.h"

#include <errno.h>
#include <string.h>

/** @brief AES block size, bytes. */
#define AES_BLOCK_LEN 16u
/** @brief CMAC/CCM\* subkey constant Rb for the 128-bit block. */
#define CMAC_RB 0x87u

/* ── little byte helpers (no Zephyr on the host TU) ─────────────────────── */

/**
 * @brief Store v big-endian into out (4 bytes).
 * @param out Destination buffer for the 4 big-endian bytes.
 * @param v Value to store.
 */
static void put_be32(uint8_t *out, uint32_t v)
{
	out[0] = (uint8_t)(v >> 24);
	out[1] = (uint8_t)(v >> 16);
	out[2] = (uint8_t)(v >> 8);
	out[3] = (uint8_t)v;
}

/**
 * @brief Load a big-endian uint32 from in (4 bytes).
 * @param in Source buffer holding 4 big-endian bytes.
 * @return The decoded uint32 value.
 */
static uint32_t get_be32(const uint8_t *in)
{
	return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
	       ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

/**
 * @brief XOR n bytes: out = a ^ b (out may alias a).
 * @param out Destination buffer for the XOR result.
 * @param a First input buffer.
 * @param b Second input buffer.
 * @param n Number of bytes to XOR.
 */
static void xor_bytes(uint8_t *out, const uint8_t *a, const uint8_t *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		out[i] = a[i] ^ b[i];
	}
}

/* ── AES-CMAC ───────────────────────────────────────────────────────────── */

/**
 * @brief One-bit left shift of a 128-bit big-endian block.
 * @param in Input 128-bit block.
 * @param out Output buffer receiving the shifted block.
 * @return The bit shifted out of the most significant bit position.
 */
static uint8_t block_lshift1(const uint8_t in[AES_BLOCK_LEN],
			     uint8_t out[AES_BLOCK_LEN])
{
	uint8_t carry = 0;
	for (size_t i = AES_BLOCK_LEN; i-- > 0;) {
		uint8_t next = (uint8_t)(in[i] >> 7);
		out[i] = (uint8_t)((in[i] << 1) | carry);
		carry = next;
	}
	return (uint8_t)(in[0] >> 7);
}

/**
 * @brief CMAC subkey derivation: K = (L << 1), XOR Rb if the top bit was set.
 * @param l Input 128-bit value (L, or a previously derived subkey).
 * @param out Output buffer receiving the derived subkey.
 */
static void cmac_subkey(const uint8_t l[AES_BLOCK_LEN], uint8_t out[AES_BLOCK_LEN])
{
	uint8_t msb = block_lshift1(l, out);
	if (msb) {
		out[AES_BLOCK_LEN - 1] ^= CMAC_RB;
	}
}

/**
 * @brief AES-CMAC over msg (msg may be NULL iff msg_len is 0).
 * @param key AES key bytes.
 * @param key_bits AES key size in bits.
 * @param msg Message to authenticate; may be NULL only if msg_len is 0.
 * @param msg_len Length of msg in bytes.
 * @param tag Output buffer receiving the CMAC tag.
 * @return 0 on success; -EINVAL for invalid arguments; propagated error from AES encryption otherwise.
 */
int ccc_aes_cmac(const uint8_t *key, size_t key_bits,
		 const uint8_t *msg, size_t msg_len,
		 uint8_t tag[CCC_CMAC_TAG_LEN])
{
	uint8_t l[AES_BLOCK_LEN];
	uint8_t k1[AES_BLOCK_LEN];
	uint8_t k2[AES_BLOCK_LEN];
	uint8_t x[AES_BLOCK_LEN] = {0};
	uint8_t last[AES_BLOCK_LEN];
	int rc;

	if (key == NULL || tag == NULL || (msg == NULL && msg_len > 0)) {
		return -EINVAL;
	}

	/* Subkeys from L = AES-Encrypt(K, 0^128). */
	memset(l, 0, sizeof(l));
	rc = crypto_aes_ecb_encrypt(key, key_bits, l, l);
	if (rc != 0) {
		return rc;
	}
	cmac_subkey(l, k1);
	cmac_subkey(k1, k2);

	/* Whole blocks except the last are chained straight in. */
	size_t full = (msg_len == 0) ? 0 : (msg_len - 1) / AES_BLOCK_LEN;
	for (size_t i = 0; i < full; i++) {
		xor_bytes(x, x, &msg[i * AES_BLOCK_LEN], AES_BLOCK_LEN);
		rc = crypto_aes_ecb_encrypt(key, key_bits, x, x);
		if (rc != 0) {
			return rc;
		}
	}

	/* Last block: complete → XOR K1; else 0x80-pad → XOR K2. */
	size_t rem = msg_len - full * AES_BLOCK_LEN; /* 1..16, or 0 iff msg_len==0 */
	if (msg_len != 0 && rem == AES_BLOCK_LEN) {
		xor_bytes(last, &msg[full * AES_BLOCK_LEN], k1, AES_BLOCK_LEN);
	} else {
		uint8_t padded[AES_BLOCK_LEN];
		memset(padded, 0, sizeof(padded));
		if (rem > 0) {
			memcpy(padded, &msg[full * AES_BLOCK_LEN], rem);
		}
		padded[rem] = 0x80u;
		xor_bytes(last, padded, k2, AES_BLOCK_LEN);
	}
	xor_bytes(x, x, last, AES_BLOCK_LEN);
	rc = crypto_aes_ecb_encrypt(key, key_bits, x, x);
	if (rc != 0) {
		return rc;
	}

	memcpy(tag, x, CCC_CMAC_TAG_LEN);
	return 0;
}

/* ── Counter-mode KDF block (single 128-bit PRF output) ─────────────────── */

/** @brief Largest KDF fixed-input we assemble: ctr(4)+"URSK_KT"(7)+0x00+ctx(32)+L(4). */
#define KDF_INPUT_MAX 48u

/**
 * @brief One counter-mode CMAC block: CMAC(kdk, counter || label || 0x00 || context || l_bits).
 * @param kdk Key-derivation key.
 * @param kdk_bits kdk size in bits.
 * @param counter Block counter value.
 * @param label KDF label bytes.
 * @param label_len Length of label in bytes.
 * @param context KDF context bytes (may be omitted if ctx_len is 0).
 * @param ctx_len Length of context in bytes.
 * @param l_bits Requested output length in bits, encoded into the KDF input.
 * @param out Output buffer receiving the CMAC block.
 * @return 0 on success; -E2BIG if the assembled KDF input exceeds the internal buffer; propagated CMAC error otherwise.
 */
static int kdf108_block(const uint8_t *kdk, size_t kdk_bits, uint32_t counter,
			const uint8_t *label, size_t label_len,
			const uint8_t *context, size_t ctx_len,
			uint32_t l_bits, uint8_t out[AES_BLOCK_LEN])
{
	uint8_t msg[KDF_INPUT_MAX];
	size_t n = 0;

	if (4u + label_len + 1u + ctx_len + 4u > sizeof(msg)) {
		return -E2BIG;
	}

	put_be32(&msg[n], counter);
	n += 4;
	memcpy(&msg[n], label, label_len);
	n += label_len;
	msg[n++] = 0x00u;
	if (ctx_len > 0) {
		memcpy(&msg[n], context, ctx_len);
		n += ctx_len;
	}
	put_be32(&msg[n], l_bits);
	n += 4;

	return ccc_aes_cmac(kdk, kdk_bits, msg, n, out);
}

/* ── Labels and shared context fragments ────────────────────────────────── */

/** @brief Label "UPSK" (0x5550534B). */
static const uint8_t LBL_UPSK[4] = {0x55, 0x50, 0x53, 0x4B};
/** @brief Label "URSK" (0x5552534B). */
static const uint8_t LBL_URSK[4] = {0x55, 0x52, 0x53, 0x4B};
/** @brief Label "UDSK" (0x5544534B). */
static const uint8_t LBL_UDSK[4] = {0x55, 0x44, 0x53, 0x4B};
/** @brief Label "SALT" (0x53414C54). */
static const uint8_t LBL_SALT[4] = {0x53, 0x41, 0x4C, 0x54};
/** @brief Label "URSK_KT" (0x5552534B5F4B54). */
static const uint8_t LBL_URSK_KT[7] = {0x55, 0x52, 0x53, 0x4B, 0x5F, 0x4B, 0x54};
/** @brief Label "UAD" (0x554144). */
static const uint8_t LBL_UAD[3] = {0x55, 0x41, 0x44};

/** @brief 3-byte zero context tail carried by mUPSK/Salt/mURSK/dURSK/dUDSK. */
static const uint8_t CTX_ZERO3[3] = {0x00, 0x00, 0x00};

/** @brief `[L]_32` = 0x180 (384): the 3-block mUPSK material (mUPSK1 || mUPSK2). */
#define L_UPSK 0x00000180u
/** @brief `[L]_32` = 0x100 (256): the 2-block mURSK / URSK_KT material. */
#define L_256BIT 0x00000100u
/** @brief `[L]_32` = 0x80 (128): the 1-block Salt / dURSK / dUDSK / UAD material. */
#define L_128BIT 0x00000080u

/**
 * @brief Derive mUPSK1, the SP0 Pre-POLL AES-CCM* key.
 * @param ursk 256-bit URSK input key.
 * @param out Output buffer receiving mUPSK1.
 * @return 0 on success; -EINVAL if ursk or out is NULL.
 */
int ccc_derive_mupsk1(const uint8_t ursk[CCC_URSK_LEN],
		      uint8_t out[CCC_MUPSK1_LEN])
{
	if (ursk == NULL || out == NULL) {
		return -EINVAL;
	}
	return kdf108_block(ursk, 256, 1, LBL_UPSK, sizeof(LBL_UPSK),
			    CTX_ZERO3, sizeof(CTX_ZERO3), L_UPSK, out);
}

/**
 * @brief Derive mUPSK2, the seed for the UWB-address KDF.
 * @param ursk 256-bit URSK input key.
 * @param out Output buffer receiving mUPSK2.
 * @return 0 on success; -EINVAL if ursk or out is NULL; propagated error from the underlying KDF block otherwise.
 */
int ccc_derive_mupsk2(const uint8_t ursk[CCC_URSK_LEN],
		      uint8_t out[CCC_MUPSK2_LEN])
{
	int rc;

	if (ursk == NULL || out == NULL) {
		return -EINVAL;
	}
	/* mUPSK: one KDF (L=384) over counters 1..3; mUPSK1=ctr1, mUPSK2=ctr2||ctr3. */
	rc = kdf108_block(ursk, 256, 2, LBL_UPSK, sizeof(LBL_UPSK),
			  CTX_ZERO3, sizeof(CTX_ZERO3), L_UPSK, &out[0]);
	if (rc != 0) {
		return rc;
	}
	return kdf108_block(ursk, 256, 3, LBL_UPSK, sizeof(LBL_UPSK),
			    CTX_ZERO3, sizeof(CTX_ZERO3), L_UPSK, &out[16]);
}

/**
 * @brief Derive mURSK, the ranging-key seed feeding URSK_KT.
 * @param ursk 256-bit URSK input key.
 * @param out Output buffer receiving mURSK.
 * @return 0 on success; -EINVAL if ursk or out is NULL; propagated error from the underlying KDF block otherwise.
 */
int ccc_derive_mursk(const uint8_t ursk[CCC_URSK_LEN],
		     uint8_t out[CCC_MURSK_LEN])
{
	int rc;

	if (ursk == NULL || out == NULL) {
		return -EINVAL;
	}
	rc = kdf108_block(ursk, 256, 1, LBL_URSK, sizeof(LBL_URSK),
			  CTX_ZERO3, sizeof(CTX_ZERO3), L_256BIT, &out[0]);
	if (rc != 0) {
		return rc;
	}
	return kdf108_block(ursk, 256, 2, LBL_URSK, sizeof(LBL_URSK),
			    CTX_ZERO3, sizeof(CTX_ZERO3), L_256BIT, &out[16]);
}

/**
 * @brief Derive SaltedHash from the serialized ranging configuration.
 * @param ursk 256-bit URSK input key.
 * @param ranging_config Serialized ranging configuration bytes (may be omitted if rc_len is 0).
 * @param rc_len Length of ranging_config in bytes.
 * @param out Output buffer receiving SaltedHash.
 * @return 0 on success; -EINVAL for invalid arguments; propagated error from the underlying KDF or CMAC otherwise.
 */
int ccc_derive_salted_hash(const uint8_t ursk[CCC_URSK_LEN],
			   const uint8_t *ranging_config, size_t rc_len,
			   uint8_t out[CCC_SALTED_HASH_LEN])
{
	uint8_t salt[CCC_CMAC_TAG_LEN];
	uint8_t padded_salt[32];
	int rc;

	if (ursk == NULL || out == NULL || (ranging_config == NULL && rc_len > 0)) {
		return -EINVAL;
	}

	rc = kdf108_block(ursk, 256, 1, LBL_SALT, sizeof(LBL_SALT),
			  CTX_ZERO3, sizeof(CTX_ZERO3), L_128BIT, salt);
	if (rc != 0) {
		return rc;
	}
	/* PaddedSalt = 0x00..00 (128b) || Salt → the 256-bit CMAC key. */
	memset(padded_salt, 0, 16);
	memcpy(&padded_salt[16], salt, sizeof(salt));

	return ccc_aes_cmac(padded_salt, 256, ranging_config, rc_len, out);
}

/**
 * @brief Derive URSK_KT, generated once per ranging cycle and keyed by the STS index.
 * @param mursk mURSK input key.
 * @param sts_index STS index for the current ranging cycle, expanded bitwise into the KDF context.
 * @param out Output buffer receiving URSK_KT.
 * @return 0 on success; -EINVAL if mursk or out is NULL; propagated error from the underlying KDF block otherwise.
 */
int ccc_derive_ursk_kt(const uint8_t mursk[CCC_MURSK_LEN],
		       uint32_t sts_index,
		       uint8_t out[CCC_URSK_KT_LEN])
{
	uint8_t ctx[32];
	int rc;

	if (mursk == NULL || out == NULL) {
		return -EINVAL;
	}
	/* Context = STS_Index bits 31..0, each zero-extended to one byte. */
	for (int b = 0; b < 32; b++) {
		ctx[b] = (uint8_t)((sts_index >> (31 - b)) & 1u);
	}

	rc = kdf108_block(mursk, 256, 1, LBL_URSK_KT, sizeof(LBL_URSK_KT),
			  ctx, sizeof(ctx), L_256BIT, &out[0]);
	if (rc != 0) {
		return rc;
	}
	return kdf108_block(mursk, 256, 2, LBL_URSK_KT, sizeof(LBL_URSK_KT),
			    ctx, sizeof(ctx), L_256BIT, &out[16]);
}

/**
 * @brief Shared dURSK/dUDSK derivation body: CMAC(URSK_KT, ctr1 || label || 0x00 || SaltedHash || 0x000000 || 0x80).
 * @param ursk_kt URSK_KT input key for the current ranging cycle.
 * @param label 4-byte KDF label distinguishing dURSK from dUDSK.
 * @param salted_hash SaltedHash of the ranging configuration.
 * @param out Output buffer receiving the derived key.
 * @return Result of the underlying KDF block derivation.
 */
static int derive_dkey(const uint8_t ursk_kt[CCC_URSK_KT_LEN],
		       const uint8_t label[4],
		       const uint8_t salted_hash[CCC_SALTED_HASH_LEN],
		       uint8_t out[CCC_DURSK_LEN])
{
	uint8_t ctx[CCC_SALTED_HASH_LEN + 3];

	memcpy(ctx, salted_hash, CCC_SALTED_HASH_LEN);
	memcpy(&ctx[CCC_SALTED_HASH_LEN], CTX_ZERO3, sizeof(CTX_ZERO3));

	return kdf108_block(ursk_kt, 256, 1, label, 4, ctx, sizeof(ctx),
			    L_128BIT, out);
}

/**
 * @brief Derive dURSK, per-cycle STS key material.
 * @param ursk_kt URSK_KT input key for the current ranging cycle.
 * @param salted_hash SaltedHash of the ranging configuration.
 * @param out Output buffer receiving dURSK.
 * @return 0 on success; -EINVAL if any argument is NULL; propagated error from the underlying derivation otherwise.
 */
int ccc_derive_dursk(const uint8_t ursk_kt[CCC_URSK_KT_LEN],
		     const uint8_t salted_hash[CCC_SALTED_HASH_LEN],
		     uint8_t out[CCC_DURSK_LEN])
{
	if (ursk_kt == NULL || salted_hash == NULL || out == NULL) {
		return -EINVAL;
	}
	return derive_dkey(ursk_kt, LBL_URSK, salted_hash, out);
}

/**
 * @brief Derive dUDSK, per-cycle SP0 timestamp-frame key.
 * @param ursk_kt URSK_KT input key for the current ranging cycle.
 * @param salted_hash SaltedHash of the ranging configuration.
 * @param out Output buffer receiving dUDSK.
 * @return 0 on success; -EINVAL if any argument is NULL; propagated error from the underlying derivation otherwise.
 */
int ccc_derive_dudsk(const uint8_t ursk_kt[CCC_URSK_KT_LEN],
		     const uint8_t salted_hash[CCC_SALTED_HASH_LEN],
		     uint8_t out[CCC_DUDSK_LEN])
{
	if (ursk_kt == NULL || salted_hash == NULL || out == NULL) {
		return -EINVAL;
	}
	return derive_dkey(ursk_kt, LBL_UDSK, salted_hash, out);
}

/**
 * @brief Derive STS-V (phyHrpUwbStsV), the per-PPDU STS IV for the DW3000.
 * @param salted_hash SaltedHash of the ranging configuration; out may alias this buffer.
 * @param sts_index STS index added (mod 2^32) into the big-endian word at bytes 8..11.
 * @param out Output buffer receiving STS-V.
 * @return 0 on success; -EINVAL if salted_hash or out is NULL.
 */
int ccc_derive_sts_v(const uint8_t salted_hash[CCC_SALTED_HASH_LEN],
		     uint32_t sts_index,
		     uint8_t out[CCC_STS_V_LEN])
{
	if (salted_hash == NULL || out == NULL) {
		return -EINVAL;
	}
	/* STS-V = SaltedHash with big-endian word[2] (bytes 8..11) advanced by STS_Index mod 2^32; other words pass through. */
	memcpy(out, salted_hash, CCC_STS_V_LEN);
	put_be32(&out[8], get_be32(&out[8]) + sts_index);
	return 0;
}

/**
 * @brief Derive UAD, the raw UWB-address derivation output.
 * @param mupsk2 mUPSK2 input key.
 * @param sts_index0 Initial STS index, encoded big-endian into the KDF context.
 * @param out Output buffer receiving UAD.
 * @return 0 on success; -EINVAL if mupsk2 or out is NULL; propagated error from the underlying KDF block otherwise.
 */
int ccc_derive_uad(const uint8_t mupsk2[CCC_MUPSK2_LEN],
		   uint32_t sts_index0,
		   uint8_t out[CCC_UAD_LEN])
{
	uint8_t ctx[4];

	if (mupsk2 == NULL || out == NULL) {
		return -EINVAL;
	}
	put_be32(ctx, sts_index0); /* STSIndex0[0:32], big-endian */
	return kdf108_block(mupsk2, 256, 1, LBL_UAD, sizeof(LBL_UAD),
			    ctx, sizeof(ctx), L_128BIT, out);
}

/**
 * @brief If addr is a reserved all-ones value (0xFFFF/0xFFFE for 2 bytes, 0xFF..FF for other lengths), clear its top bit.
 * @param addr Address buffer checked and modified in place.
 * @param len Length of addr in bytes.
 */
static void remap_if_reserved(uint8_t *addr, size_t len)
{
	int reserved = 1;

	if (len == 2) {
		/* 0xFFFF and 0xFFFE are the reserved short values. */
		reserved = (addr[0] == 0xFFu) && (addr[1] >= 0xFEu);
	} else {
		for (size_t i = 0; i < len; i++) {
			if (addr[i] != 0xFFu) {
				reserved = 0;
				break;
			}
		}
	}
	if (reserved) {
		addr[0] &= 0x7Fu;
	}
}

/**
 * @brief Split UAD into the UWB addresses (KeySource, destination short address, source long address).
 * @param uad UAD bytes to split.
 * @param keysource Output buffer receiving the KeySource address.
 * @param dest_short_addr Output buffer receiving the destination short address.
 * @param src_long_addr Output buffer receiving the source long address.
 * @return 0 on success; -EINVAL if uad or any output pointer is NULL.
 */
int ccc_uad_addresses(const uint8_t uad[CCC_UAD_LEN],
		      uint8_t keysource[CCC_KEYSOURCE_LEN],
		      uint8_t dest_short_addr[CCC_DEST_SHORT_ADDR_LEN],
		      uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN])
{
	uint8_t ks_low[2];
	uint8_t ks_high[2];

	if (uad == NULL || keysource == NULL || dest_short_addr == NULL ||
	    src_long_addr == NULL) {
		return -EINVAL;
	}

	/* UAD splits sequentially: KeySourceLow, KeySourceHigh, DestShort, SourceLong (trailing 2 bytes unused). */
	memcpy(ks_low, &uad[0], 2);
	memcpy(ks_high, &uad[2], 2);
	memcpy(dest_short_addr, &uad[4], CCC_DEST_SHORT_ADDR_LEN);
	memcpy(src_long_addr, &uad[6], CCC_SRC_LONG_ADDR_LEN);

	remap_if_reserved(ks_low, 2);
	remap_if_reserved(ks_high, 2);
	remap_if_reserved(dest_short_addr, CCC_DEST_SHORT_ADDR_LEN);
	remap_if_reserved(src_long_addr, CCC_SRC_LONG_ADDR_LEN);

	/* KeySource = KeySourceHigh || KeySourceLow. */
	memcpy(&keysource[0], ks_high, 2);
	memcpy(&keysource[2], ks_low, 2);
	return 0;
}

/* ── AES-CCM*, SP0 Security Level 6 ─────────────────────────────────────── */

/** @brief SP0 nonce Security Level field (6 = ENC-MIC-64). */
#define SP0_SEC_LEVEL 0x06u
/** @brief CCM length-field size L (2 → 16-bit frame lengths). */
#define CCM_L 2u
/** @brief Largest AAD/payload we buffer for the SP0 CBC-MAC. */
#define CCM_SCRATCH_MAX 128u

/**
 * @brief Build the SP0 CCM* nonce: SrcLongAddr || FrameCounter(BE) || SecLevel.
 * @param nonce Output buffer receiving the assembled nonce.
 * @param src_long_addr Source long address included in the nonce.
 * @param frame_counter Frame counter encoded big-endian into the nonce.
 */
static void sp0_nonce(uint8_t nonce[CCC_SP0_NONCE_LEN],
		      const uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN],
		      uint32_t frame_counter)
{
	memcpy(&nonce[0], src_long_addr, CCC_SRC_LONG_ADDR_LEN);
	put_be32(&nonce[8], frame_counter);
	nonce[12] = SP0_SEC_LEVEL;
}

/**
 * @brief Compute the CCM* CBC-MAC over B0 || l(a)||MHR || payload, zero-padded per block.
 * @param key AES-128 key used for the CBC-MAC.
 * @param nonce SP0 CCM* nonce for this frame.
 * @param mhr MAC header bytes forming the additional authenticated data (may be omitted if mhr_len is 0).
 * @param mhr_len Length of mhr in bytes.
 * @param payload Payload bytes to authenticate.
 * @param payload_len Length of payload in bytes.
 * @param tag Output buffer receiving the computed CBC-MAC tag.
 * @return 0 on success; propagated error from AES encryption otherwise.
 */
static int sp0_cbc_mac(const uint8_t key[CCC_MUPSK1_LEN],
		       const uint8_t nonce[CCC_SP0_NONCE_LEN],
		       const uint8_t *mhr, size_t mhr_len,
		       const uint8_t *payload, size_t payload_len,
		       uint8_t tag[AES_BLOCK_LEN])
{
	uint8_t blk[AES_BLOCK_LEN];
	int rc;

	/* B0 = flags || nonce || l(m); flags Adata|M'|L' = 0x40|0x18|0x01 = 0x59. */
	blk[0] = (uint8_t)(0x40u | (((CCC_SP0_MIC_LEN - 2u) / 2u) << 3) | (CCM_L - 1u));
	memcpy(&blk[1], nonce, CCC_SP0_NONCE_LEN);
	blk[14] = (uint8_t)(payload_len >> 8);
	blk[15] = (uint8_t)payload_len;
	rc = crypto_aes_ecb_encrypt(key, 128, blk, tag); /* X1 = E(B0). */
	if (rc != 0) {
		return rc;
	}

	/* AAD stream = [l(a)]_16 || MHR, processed in zero-padded blocks. */
	if (mhr_len > 0) {
		uint8_t aad[2u + CCM_SCRATCH_MAX];
		size_t aad_len = 2u + mhr_len;

		aad[0] = (uint8_t)(mhr_len >> 8);
		aad[1] = (uint8_t)mhr_len;
		memcpy(&aad[2], mhr, mhr_len);
		for (size_t off = 0; off < aad_len; off += AES_BLOCK_LEN) {
			size_t take = aad_len - off;

			if (take > AES_BLOCK_LEN) {
				take = AES_BLOCK_LEN;
			}
			memset(blk, 0, sizeof(blk));
			memcpy(blk, &aad[off], take);
			xor_bytes(tag, tag, blk, AES_BLOCK_LEN);
			rc = crypto_aes_ecb_encrypt(key, 128, tag, tag);
			if (rc != 0) {
				return rc;
			}
		}
	}

	/* Payload blocks, zero-padded. */
	for (size_t off = 0; off < payload_len; off += AES_BLOCK_LEN) {
		size_t take = payload_len - off;

		if (take > AES_BLOCK_LEN) {
			take = AES_BLOCK_LEN;
		}
		memset(blk, 0, sizeof(blk));
		memcpy(blk, &payload[off], take);
		xor_bytes(tag, tag, blk, AES_BLOCK_LEN);
		rc = crypto_aes_ecb_encrypt(key, 128, tag, tag);
		if (rc != 0) {
			return rc;
		}
	}
	return 0;
}

/**
 * @brief Apply CCM* CTR mode: emit the S0 keystream block, then XOR keystream S1.. over in to out (symmetric encrypt/decrypt).
 * @param key AES-128 key used for the CTR keystream.
 * @param nonce SP0 CCM* nonce for this frame.
 * @param in Input bytes to transform.
 * @param len Length of in/out in bytes.
 * @param out Output buffer receiving the transformed bytes.
 * @param s0 Output buffer receiving the S0 keystream block (used for MIC encryption).
 * @return 0 on success; propagated error from AES encryption otherwise.
 */
static int sp0_ctr(const uint8_t key[CCC_MUPSK1_LEN],
		   const uint8_t nonce[CCC_SP0_NONCE_LEN],
		   const uint8_t *in, size_t len, uint8_t *out,
		   uint8_t s0[AES_BLOCK_LEN])
{
	uint8_t ctr_blk[AES_BLOCK_LEN];
	int rc;

	ctr_blk[0] = (uint8_t)(CCM_L - 1u);
	memcpy(&ctr_blk[1], nonce, CCC_SP0_NONCE_LEN);
	ctr_blk[14] = 0;
	ctr_blk[15] = 0;
	rc = crypto_aes_ecb_encrypt(key, 128, ctr_blk, s0); /* S0 = E(A0). */
	if (rc != 0) {
		return rc;
	}
	for (size_t off = 0, i = 1; off < len; off += AES_BLOCK_LEN, i++) {
		uint8_t s[AES_BLOCK_LEN];
		size_t take = len - off;

		if (take > AES_BLOCK_LEN) {
			take = AES_BLOCK_LEN;
		}
		ctr_blk[14] = (uint8_t)(i >> 8);
		ctr_blk[15] = (uint8_t)i;
		rc = crypto_aes_ecb_encrypt(key, 128, ctr_blk, s);
		if (rc != 0) {
			return rc;
		}
		xor_bytes(&out[off], &in[off], s, take);
	}
	return 0;
}

/** @brief Constant-time inequality: 0 iff the @p n bytes are equal. */
static int sp0_ct_diff(const uint8_t *a, const uint8_t *b, size_t n)
{
	uint8_t d = 0;

	for (size_t i = 0; i < n; i++) {
		d |= (uint8_t)(a[i] ^ b[i]);
	}
	return d;
}

int ccc_sp0_encrypt(const uint8_t key[CCC_MUPSK1_LEN],
		    const uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN],
		    uint32_t frame_counter,
		    const uint8_t *mhr, size_t mhr_len,
		    const uint8_t *payload, size_t payload_len,
		    uint8_t *ciphertext_out,
		    uint8_t mic_out[CCC_SP0_MIC_LEN])
{
	uint8_t nonce[CCC_SP0_NONCE_LEN];
	uint8_t x[AES_BLOCK_LEN];
	uint8_t s0[AES_BLOCK_LEN];
	int rc;

	if (key == NULL || src_long_addr == NULL || mic_out == NULL ||
	    (mhr == NULL && mhr_len > 0) ||
	    (payload == NULL && payload_len > 0) ||
	    (ciphertext_out == NULL && payload_len > 0)) {
		return -EINVAL;
	}
	if (mhr_len >= 0xFF00u || mhr_len > CCM_SCRATCH_MAX ||
	    payload_len > CCM_SCRATCH_MAX) {
		return -E2BIG;
	}

	sp0_nonce(nonce, src_long_addr, frame_counter);
	rc = sp0_cbc_mac(key, nonce, mhr, mhr_len, payload, payload_len, x);
	if (rc != 0) {
		return rc;
	}
	rc = sp0_ctr(key, nonce, payload, payload_len, ciphertext_out, s0);
	if (rc != 0) {
		return rc;
	}
	xor_bytes(mic_out, x, s0, CCC_SP0_MIC_LEN); /* U = T ^ S0[0:M]. */
	return 0;
}

int ccc_sp0_decrypt(const uint8_t key[CCC_MUPSK1_LEN],
		    const uint8_t src_long_addr[CCC_SRC_LONG_ADDR_LEN],
		    uint32_t frame_counter,
		    const uint8_t *mhr, size_t mhr_len,
		    const uint8_t *ciphertext, size_t ciphertext_len,
		    const uint8_t mic[CCC_SP0_MIC_LEN],
		    uint8_t *payload_out)
{
	uint8_t nonce[CCC_SP0_NONCE_LEN];
	uint8_t x[AES_BLOCK_LEN];
	uint8_t s0[AES_BLOCK_LEN];
	uint8_t mic_calc[CCC_SP0_MIC_LEN];
	int rc;

	if (key == NULL || src_long_addr == NULL || mic == NULL ||
	    (mhr == NULL && mhr_len > 0) ||
	    (ciphertext == NULL && ciphertext_len > 0) ||
	    (payload_out == NULL && ciphertext_len > 0)) {
		return -EINVAL;
	}
	if (mhr_len >= 0xFF00u || mhr_len > CCM_SCRATCH_MAX ||
	    ciphertext_len > CCM_SCRATCH_MAX) {
		return -E2BIG;
	}

	sp0_nonce(nonce, src_long_addr, frame_counter);
	/* Recover the plaintext (CTR), then authenticate it (CBC-MAC). */
	rc = sp0_ctr(key, nonce, ciphertext, ciphertext_len, payload_out, s0);
	if (rc != 0) {
		return rc;
	}
	rc = sp0_cbc_mac(key, nonce, mhr, mhr_len, payload_out, ciphertext_len, x);
	if (rc != 0) {
		return rc;
	}
	xor_bytes(mic_calc, x, s0, CCC_SP0_MIC_LEN);
	if (sp0_ct_diff(mic_calc, mic, CCC_SP0_MIC_LEN) != 0) {
		/* Do not release unauthenticated plaintext. */
		if (payload_out != NULL) {
			memset(payload_out, 0, ciphertext_len);
		}
		return -EBADMSG;
	}
	return 0;
}
