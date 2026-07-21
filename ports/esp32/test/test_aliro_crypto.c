/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host known-answer test for aliro_crypto's portable SHA-256/KDF core (the
 * interop-critical key-schedule primitives) plus the URSK block extraction.
 * Pure host build, no ESP-IDF or hardware. The KDF core is compiled from the
 * exact same source as the target, so a PASS here pins the on-target behavior.
 *
 * Anchors are canonical: FIPS-180-4 (SHA-256), RFC 4231 (HMAC), RFC 5869
 * (HKDF). The X9.63 KDF is cross-checked against a direct Hash(Z|ctr|info)
 * recompute so its counter width/endianness are locked.
 */
#include <stdio.h>
#include <string.h>

#include "aliro_crypto.h"
#include "aliro_hash.h"
#include "aliro_prim.h"

static int fails;

static void hx(char *d, const uint8_t *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		sprintf(d + 2 * i, "%02x", b[i]);
	}
}

static int uh(uint8_t *d, const char *h)
{
	size_t n = strlen(h) / 2;

	for (size_t i = 0; i < n; i++) {
		unsigned v;

		sscanf(h + 2 * i, "%2x", &v);
		d[i] = (uint8_t)v;
	}
	return (int)n;
}

static int pending;

static void chk(const char *name, const uint8_t *got, size_t n, const char *want)
{
	char g[512];

	hx(g, got, n);
	if (strcmp(g, want) != 0) {
		printf("  FAIL %-20s\n    got  %s\n    want %s\n", name, g, want);
		fails++;
	} else {
		printf("  ok   %-20s %s\n", name, g);
	}
}

/* Behavior-lock: expect==NULL/"" records the value (bake it in afterwards). */
static void t_vec(const char *name, const uint8_t *got, size_t n, const char *want)
{
	if (want == NULL || *want == '\0') {
		char g[512];

		hx(g, got, n);
		printf("  REC  %-20s %s\n", name, g);
		pending++;
		return;
	}
	chk(name, got, n, want);
}

static void t_ok_(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

static void t_eq_(const char *name, long got, long want)
{
	if (got != want) {
		printf("  FAIL %-20s got %ld want %ld\n", name, got, want);
		fails++;
	} else {
		printf("  ok   %-20s %ld\n", name, got);
	}
}

#define T_OK(name, cond) t_ok_((name), (cond))
#define T_EQ(name, got, want) t_eq_((name), (long)(got), (long)(want))

int main(void)
{
	uint8_t out[64], key[64], msg[64];

	printf("== aliro_crypto: SHA-256 / HMAC / HKDF / X9.63 KATs ==\n");

	aliro_sha256("abc", 3, out);
	chk("sha256/abc", out, 32,
	    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	aliro_sha256("", 0, out);
	chk("sha256/empty", out, 32,
	    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

	/* RFC 4231 test case 2 */
	int kl = uh(key, "4a656665");

	memcpy(msg, "what do ya want for nothing?", 28);
	aliro_hmac_sha256(key, kl, msg, 28, out);
	chk("hmac/rfc4231-2", out, 32,
	    "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

	/* RFC 5869 test case 1 */
	uint8_t ikm[22], salt[13], info[10], prk[32], okm[42];

	memset(ikm, 0x0b, sizeof(ikm));
	uh(salt, "000102030405060708090a0b0c");
	uh(info, "f0f1f2f3f4f5f6f7f8f9");
	aliro_hkdf_extract(salt, sizeof(salt), ikm, sizeof(ikm), prk);
	chk("hkdf/prk", prk, 32,
	    "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
	aliro_hkdf_expand(prk, info, sizeof(info), okm, sizeof(okm));
	chk("hkdf/okm", okm, 42,
	    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");

	/* X9.63: KDF output must equal Hash(Z | ctr_be32 | info) per 32-byte block. */
	uint8_t z[24], si[16], x[40], expect[64], blk[64];
	int zl = uh(z, "96c05619d56c328ab95fe84b18264b08725b85e33fd34f08");

	uh(si, "75eef81aa3041e33b80971203d2c0c52");
	aliro_x963_kdf(z, zl, si, 16, x, 40);
	memcpy(blk, z, zl);
	blk[zl] = 0;
	blk[zl + 1] = 0;
	blk[zl + 2] = 0;
	blk[zl + 3] = 1;
	memcpy(blk + zl + 4, si, 16);
	aliro_sha256(blk, zl + 4 + 16, expect);
	blk[zl + 3] = 2;
	aliro_sha256(blk, zl + 4 + 16, expect + 32);
	{
		char a[128], b[128];

		hx(a, x, 40);
		hx(b, expect, 40);
		if (strcmp(a, b) != 0) {
			printf("  FAIL x963/consistency\n    kdf %s\n    rec %s\n", a, b);
			fails++;
		} else {
			printf("  ok   %-20s %s\n", "x963/consistency", a);
		}
	}

	/* URSK extraction: block[128..159]. */
	uint8_t block[ALIRO_KEY_BLOCK_LEN], ursk[ALIRO_URSK_LEN];

	for (size_t i = 0; i < sizeof(block); i++) {
		block[i] = (uint8_t)i;
	}
	aliro_crypto_ursk_from_block(block, ursk);
	chk("ursk/offset128", ursk, 32,
	    "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");

	printf("\n== AES-256-GCM (host double) ==\n");
	/* GCM spec Test Case 14: K=0(32), IV=0(12), P=0(16). */
	uint8_t zk[32] = { 0 }, ziv[12] = { 0 }, zp[16] = { 0 }, gct[16], gtag[16];

	aliro_aes256_gcm_encrypt(zk, ziv, 12, NULL, 0, zp, 16, gct, gtag, 16);
	chk("gcm/tc14.ct", gct, 16, "cea7403d4d606b6e074ec5d3baf39d18");
	chk("gcm/tc14.tag", gtag, 16, "d0d1c8a799996bf0265b98b5d48ab919");
	/* round-trip + tamper */
	uint8_t gpt[16];

	T_OK("gcm/decrypt", aliro_aes256_gcm_decrypt(zk, ziv, 12, NULL, 0, gct, 16,
						     gtag, 16, gpt) == 0 &&
				   memcmp(gpt, zp, 16) == 0);
	gtag[0] ^= 1;
	T_OK("gcm/tamper-rejected",
	     aliro_aes256_gcm_decrypt(zk, ziv, 12, NULL, 0, gct, 16, gtag, 16, gpt) < 0);

	printf("\n== GCM nonce construction ==\n");
	uint8_t nb[12];

	aliro_crypto_gcm_nonce(0, 0, nb);
	chk("nonce/enc0", nb, 12, "000000000000000000000000");
	aliro_crypto_gcm_nonce(1, 5, nb);
	chk("nonce/dec5", nb, 12, "000000000000000100000005");
	aliro_crypto_gcm_nonce(0, 0x01020304u, nb);
	chk("nonce/enc-ctr", nb, 12, "000000000000000001020304");

	printf("\n== secure channel (reader view: counters start at 1, §8.3.1.13) ==\n");
	uint8_t s0[32], s1[32];

	for (int i = 0; i < 32; i++) {
		s0[i] = (uint8_t)(0x10 + i);
		s1[i] = (uint8_t)(0xA0 + i);
	}
	struct aliro_secchan sc;

	aliro_secchan_init(&sc, s0, s1);
	/* Both directional counters initialise to 1 (Aliro §8.3.1.13), not 0. */
	T_EQ("init.enc-ctr", sc.enc_ctr, 1);
	T_EQ("init.dec-ctr", sc.dec_ctr, 1);
	uint8_t rmsg[19];

	memcpy(rmsg, "reader->phone hello", 19);
	uint8_t sct[19], stag[16];

	T_OK("seal.ok", aliro_secchan_seal(&sc, NULL, 0, rmsg, 19, sct, stag) == 0);
	T_EQ("seal.ctr-advanced", sc.enc_ctr, 2);
	/* phone would open the first reader command with S0 + nonce(0,1); verify. */
	uint8_t n0[12], sout[19];

	aliro_crypto_gcm_nonce(0, 1, n0);
	T_OK("seal.peer-opens",
	     aliro_aes256_gcm_decrypt(s0, n0, 12, NULL, 0, sct, 19, stag, 16, sout) == 0 &&
		     memcmp(sout, rmsg, 19) == 0);
	/* reader opens the first phone response on direction 1 (S1, nonce(1,1)) */
	uint8_t pmsg[17];

	memcpy(pmsg, "phone->reader ack", 17);
	uint8_t pct[17], ptag[16], n1[12];

	aliro_crypto_gcm_nonce(1, 1, n1);
	aliro_aes256_gcm_encrypt(s1, n1, 12, NULL, 0, pmsg, 17, pct, ptag, 16);
	uint8_t popen[17];

	T_OK("open.ok", aliro_secchan_open(&sc, NULL, 0, pct, 17, ptag, popen) == 0 &&
				memcmp(popen, pmsg, 17) == 0);
	T_EQ("open.ctr-advanced", sc.dec_ctr, 2);
	ptag[0] ^= 1;
	sc.dec_ctr = 1;
	T_OK("open.tamper-rejected",
	     aliro_secchan_open(&sc, NULL, 0, pct, 17, ptag, popen) < 0);

	printf("\n== key schedule composition ==\n");
	/* Fixed inputs -> Z, block, URSK. Cross-check the wiring and lock goldens. */
	uint8_t sec[32], txid[16], pubx[32], zc[32];

	for (int i = 0; i < 32; i++) {
		sec[i] = (uint8_t)(0x40 + i);
		pubx[i] = (uint8_t)(0x80 + i);
	}
	for (int i = 0; i < 16; i++) {
		txid[i] = (uint8_t)(0xF0 - i);
	}
	aliro_crypto_derive_z(sec, txid, zc);
	/* Z must equal SHA-256( sec | 00000001 | txid ) — independent recompute. */
	uint8_t zbuf[52], zexp[32];

	memcpy(zbuf, sec, 32);
	zbuf[32] = 0;
	zbuf[33] = 0;
	zbuf[34] = 0;
	zbuf[35] = 1;
	memcpy(zbuf + 36, txid, 16);
	aliro_sha256(zbuf, 52, zexp);
	T_OK("z=sha256(ss|1|txid)", memcmp(zc, zexp, 32) == 0);

	uint8_t saltbuf[ALIRO_SALT_MAX];
	size_t saltlen = 0;
	uint8_t rid[32];

	for (int i = 0; i < 32; i++) {
		rid[i] = (uint8_t)(0x11 * (i & 0x0f));
	}
	T_OK("salt.build",
	     aliro_salt_build(ALIRO_SALT_SESSION, txid, pubx, pubx, rid, ALIRO_IFACE_BLE,
			      0x0100, 0x02, 0x00, NULL, NULL, 0, saltbuf, &saltlen) == 0);
	/* type1 salt: 32+12+32+1(iface)+2+2+32+16+2 = 131 bytes (no a5, no s3opt). */
	T_EQ("salt.len", saltlen, 131);

	uint8_t blk160[160], urskc[32];

	aliro_crypto_derive_block(zc, saltbuf, saltlen, pubx, blk160);
	/* block must equal HKDF(salt, IKM=z, info=pubx, 160); the swapped
	 * (IKM/info) binding must differ — pins the argument wiring. */
	uint8_t blk_ok[160], blk_swap[160];

	aliro_hkdf(saltbuf, saltlen, zc, 32, pubx, 32, blk_ok, 160);
	aliro_hkdf(saltbuf, saltlen, pubx, 32, zc, 32, blk_swap, 160);
	T_OK("block=hkdf(salt,z,pubx)", memcmp(blk160, blk_ok, 160) == 0);
	T_OK("block!=swapped-binding", memcmp(blk160, blk_swap, 160) != 0);
	aliro_crypto_ursk_from_block(blk160, urskc);
	/* Behavior-lock golden (tied to the provisional salt layout): any change
	 * to the schedule or salt flips this. Not an interop vector. */
	t_vec("ursk.golden", urskc, 32,
	      "cf2687a33badda21bd48d6550c81da0be06cb6ace98df3a81ea6fb0902ad21d2");

	uint8_t ek[32], dk[32], us[32];

	aliro_crypto_split(blk160, 1, ek, dk, us);
	T_OK("split.enc=S0", memcmp(ek, blk160, 32) == 0);
	T_OK("split.dec=S1", memcmp(dk, blk160 + 32, 32) == 0);
	T_OK("split.ursk=S4", memcmp(us, blk160 + 128, 32) == 0);
	aliro_crypto_split(blk160, 0, ek, dk, us);
	T_OK("split.noC.enc=S1", memcmp(ek, blk160 + 32, 32) == 0);
	T_OK("split.noC.dec=S2", memcmp(dk, blk160 + 64, 32) == 0);

	if (fails) {
		printf("\nRESULT: %d FAIL\n", fails);
		return 1;
	}
	if (pending) {
		printf("\nRESULT: RECORD (%d value(s) to bake in)\n", pending);
		return 2;
	}
	printf("\nRESULT: PASS\n");
	return 0;
}
