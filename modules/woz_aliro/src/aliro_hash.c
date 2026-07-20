// Self-contained SHA-256, HMAC-SHA256, HKDF, and ANSI-X9.63 KDF implementation for the ESP32-IDF
// Aliro crypto port, with no external crypto library dependency.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Portable SHA-256 + HMAC/HKDF/X9.63-KDF. See aliro_hash.h.
 */
#include "aliro_hash.h"

#include <string.h>

// Rotate a 32-bit word right by n bits.
static uint32_t ror32(uint32_t x, unsigned n)
{
	return (x >> n) | (x << (32 - n));
}

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
	0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
	0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
	0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
	0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
	0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
	0xc67178f2,
};

// Compresses one 64-byte message block into the running SHA-256 state h, per FIPS 180-4.
// h: 8-word running hash state, updated in place.
// p: 64-byte input block.
static void sha256_block(uint32_t h[8], const uint8_t p[64])
{
	uint32_t w[64];

	for (int i = 0; i < 16; i++) {
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
		       (uint32_t)p[i * 4 + 2] << 8 | (uint32_t)p[i * 4 + 3];
	}
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);

		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
	uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

	for (int i = 0; i < 64; i++) {
		uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t t1 = hh + S1 + ch + K[i] + w[i];
		uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = S0 + maj;

		hh = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
	h[5] += f;
	h[6] += g;
	h[7] += hh;
}

// Initialize a streaming SHA-256 context with the FIPS 180-4 initial hash values.
// Resets total byte count and internal buffer length to zero; must be called before feeding data.
void aliro_sha256_init(struct aliro_sha256 *s)
{
	s->h[0] = 0x6a09e667;
	s->h[1] = 0xbb67ae85;
	s->h[2] = 0x3c6ef372;
	s->h[3] = 0xa54ff53a;
	s->h[4] = 0x510e527f;
	s->h[5] = 0x9b05688c;
	s->h[6] = 0x1f83d9ab;
	s->h[7] = 0x5be0cd19;
	s->total = 0;
	s->buflen = 0;
}

// Feeds len bytes of data into a streaming SHA-256 context, buffering a partial block and
// compressing full 64-byte blocks as they accumulate. s: context to update; total byte count and
// internal buffer are updated in place. data: bytes to hash; may be split across multiple calls.
// len: number of bytes in data.
void aliro_sha256_update(struct aliro_sha256 *s, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;

	s->total += len;
	if (s->buflen) {
		size_t n = ALIRO_SHA256_BLOCK - s->buflen;

		if (n > len) {
			n = len;
		}
		memcpy(s->buf + s->buflen, p, n);
		s->buflen += n;
		p += n;
		len -= n;
		if (s->buflen == ALIRO_SHA256_BLOCK) {
			sha256_block(s->h, s->buf);
			s->buflen = 0;
		}
	}
	while (len >= ALIRO_SHA256_BLOCK) {
		sha256_block(s->h, p);
		p += ALIRO_SHA256_BLOCK;
		len -= ALIRO_SHA256_BLOCK;
	}
	if (len) {
		memcpy(s->buf, p, len);
		s->buflen = len;
	}
}

// Finalizes a streaming SHA-256 computation, applying FIPS 180-4 padding and the big-endian
// bit-length suffix, and writes the 32-byte digest to out. s: context to finalize; consumed by this
// call, must not be reused afterward without re-initializing. out: 32-byte buffer that receives the
// digest.
void aliro_sha256_final(struct aliro_sha256 *s, uint8_t out[ALIRO_SHA256_LEN])
{
	uint64_t bits = s->total * 8;
	uint8_t pad = 0x80;

	aliro_sha256_update(s, &pad, 1);
	pad = 0;
	while (s->buflen != 56) {
		aliro_sha256_update(s, &pad, 1);
	}
	uint8_t lenbe[8];

	for (int i = 0; i < 8; i++) {
		lenbe[i] = (uint8_t)(bits >> (56 - i * 8));
	}
	aliro_sha256_update(s, lenbe, 8);
	for (int i = 0; i < 8; i++) {
		out[i * 4] = (uint8_t)(s->h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
		out[i * 4 + 3] = (uint8_t)s->h[i];
	}
}

void aliro_sha256(const void *data, size_t len, uint8_t out[ALIRO_SHA256_LEN])
{
	struct aliro_sha256 s;

	aliro_sha256_init(&s);
	aliro_sha256_update(&s, data, len);
	aliro_sha256_final(&s, out);
}

void aliro_hmac_sha256(const uint8_t *key, size_t key_len, const void *msg, size_t msg_len,
		       uint8_t out[ALIRO_SHA256_LEN])
{
	uint8_t k[ALIRO_SHA256_BLOCK];
	uint8_t ipad[ALIRO_SHA256_BLOCK], opad[ALIRO_SHA256_BLOCK];
	uint8_t inner[ALIRO_SHA256_LEN];
	struct aliro_sha256 s;

	memset(k, 0, sizeof(k));
	if (key_len > ALIRO_SHA256_BLOCK) {
		aliro_sha256(key, key_len, k);
	} else if (key_len) {
		memcpy(k, key, key_len);
	}
	for (size_t i = 0; i < ALIRO_SHA256_BLOCK; i++) {
		ipad[i] = k[i] ^ 0x36;
		opad[i] = k[i] ^ 0x5c;
	}
	aliro_sha256_init(&s);
	aliro_sha256_update(&s, ipad, sizeof(ipad));
	aliro_sha256_update(&s, msg, msg_len);
	aliro_sha256_final(&s, inner);

	aliro_sha256_init(&s);
	aliro_sha256_update(&s, opad, sizeof(opad));
	aliro_sha256_update(&s, inner, sizeof(inner));
	aliro_sha256_final(&s, out);
}

void aliro_hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
			uint8_t prk[ALIRO_SHA256_LEN])
{
	uint8_t zero[ALIRO_SHA256_LEN] = {0};

	if (salt == NULL || salt_len == 0) {
		salt = zero;
		salt_len = sizeof(zero);
	}
	aliro_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

// HKDF-SHA256 expand step (RFC 5869): derives out_len bytes of output keying material from a
// pseudorandom key and context info. prk: 32-byte pseudorandom key, typically from
// aliro_hkdf_extract. info: context/application-specific info bytes, may be NULL if info_len is 0.
// info_len: length of info in bytes.
// out: buffer that receives out_len bytes of derived key material.
// out_len: number of bytes to produce; must not exceed 255 * 32 bytes (255 * ALIRO_SHA256_LEN).
// Returns 0 on success, or -1 if out_len exceeds the RFC 5869 maximum.
int aliro_hkdf_expand(const uint8_t prk[ALIRO_SHA256_LEN], const uint8_t *info, size_t info_len,
		      uint8_t *out, size_t out_len)
{
	uint8_t t[ALIRO_SHA256_LEN];
	size_t tlen = 0;
	uint8_t counter = 1;
	size_t done = 0;

	if (out_len > 255u * ALIRO_SHA256_LEN) {
		return -1;
	}
	while (done < out_len) {
		struct aliro_sha256 s;
		uint8_t ipad[ALIRO_SHA256_BLOCK], opad[ALIRO_SHA256_BLOCK];
		uint8_t inner[ALIRO_SHA256_LEN];

		/* HMAC(prk, T(n-1) | info | counter) inlined to keep T(n-1). */
		for (size_t i = 0; i < ALIRO_SHA256_BLOCK; i++) {
			uint8_t kb = i < ALIRO_SHA256_LEN ? prk[i] : 0;

			ipad[i] = kb ^ 0x36;
			opad[i] = kb ^ 0x5c;
		}
		aliro_sha256_init(&s);
		aliro_sha256_update(&s, ipad, sizeof(ipad));
		aliro_sha256_update(&s, t, tlen);
		aliro_sha256_update(&s, info, info_len);
		aliro_sha256_update(&s, &counter, 1);
		aliro_sha256_final(&s, inner);
		aliro_sha256_init(&s);
		aliro_sha256_update(&s, opad, sizeof(opad));
		aliro_sha256_update(&s, inner, sizeof(inner));
		aliro_sha256_final(&s, t);
		tlen = ALIRO_SHA256_LEN;

		size_t n = out_len - done;

		if (n > ALIRO_SHA256_LEN) {
			n = ALIRO_SHA256_LEN;
		}
		memcpy(out + done, t, n);
		done += n;
		counter++;
	}
	return 0;
}

// HKDF-SHA256 (RFC 5869): extracts a pseudorandom key from salt and input keying material, then
// expands it to out_len bytes of output. salt: salt bytes for the extract step. salt_len: length of
// salt in bytes. ikm: input keying material. ikm_len: length of ikm in bytes. info:
// context/application-specific info bytes for the expand step. info_len: length of info in bytes.
// out: buffer that receives out_len bytes of derived key material.
// out_len: number of bytes to produce; must not exceed 255 * 32 bytes, per aliro_hkdf_expand.
// Returns 0 on success, or -1 if out_len exceeds the RFC 5869 maximum.
int aliro_hkdf(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
	       const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len)
{
	uint8_t prk[ALIRO_SHA256_LEN];

	aliro_hkdf_extract(salt, salt_len, ikm, ikm_len, prk);
	return aliro_hkdf_expand(prk, info, info_len, out, out_len);
}

int aliro_x963_kdf(const uint8_t *z, size_t z_len, const uint8_t *info, size_t info_len,
		   uint8_t *out, size_t out_len)
{
	uint32_t counter = 1;
	size_t done = 0;

	if (out_len > 0xffffffffu - ALIRO_SHA256_LEN) {
		return -1;
	}
	while (done < out_len) {
		struct aliro_sha256 s;
		uint8_t cbe[4];
		uint8_t digest[ALIRO_SHA256_LEN];

		cbe[0] = (uint8_t)(counter >> 24);
		cbe[1] = (uint8_t)(counter >> 16);
		cbe[2] = (uint8_t)(counter >> 8);
		cbe[3] = (uint8_t)counter;

		aliro_sha256_init(&s);
		aliro_sha256_update(&s, z, z_len);
		aliro_sha256_update(&s, cbe, sizeof(cbe));
		if (info && info_len) {
			aliro_sha256_update(&s, info, info_len);
		}
		aliro_sha256_final(&s, digest);

		size_t n = out_len - done;

		if (n > ALIRO_SHA256_LEN) {
			n = ALIRO_SHA256_LEN;
		}
		memcpy(out + done, digest, n);
		done += n;
		counter++;
	}
	return 0;
}
