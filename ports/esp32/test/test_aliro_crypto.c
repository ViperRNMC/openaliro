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

#include "aliro_advtag.h"
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

	printf("\n== expedited-fast phase (Aliro §14.3/§14.4 worked example) ==\n");
	{
		/* §14.3: Kpersistent = derive_key32(IKM=Kdh, salt=salt_persistent,
		 * info=credential_ephemeral_pub_x). salt_persistent (type 2) appends the
		 * Access Credential public-key X. NFC transport (iface 0x5E), standard
		 * AUTH0 flag 0x0001 (command_parameters 0x00 || auth_policy 0x01). */
		uint8_t kdh[32], rgik_x[32], reph_x[32], f_rid[32], s3opt[32], a5[16];
		uint8_t f_txid[16], ceph_x[32], fsalt[ALIRO_SALT_MAX], kpers[32];
		size_t fsalt_len = 0;
		int a5n = uh(a5, "a508800200005c020100");

		uh(kdh, "cd227f01f917ad1dd5252db51c5ad3da1c3028be750a0f4e69c6a5624fca271c");
		uh(rgik_x, "b62d9b8f494f2f43a07a7db7e965865d04feeabe4e9c3b8a2f5a544ee2a9c60f");
		uh(reph_x, "9696afe33de58b7d3253d1cba86d14147c16d455e8a27373b38d454af21b70e7");
		uh(f_rid, "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100");
		uh(s3opt, "88f6f8f2f1e35a58879e72d9ea81957e8964c3d3c566eb9d41c83d0d8c63a230");
		uh(f_txid, "4165a83667ad0af5ab115247424822e0");
		uh(ceph_x, "5d75ab60136a2c54ff27b799ee157f3f3329435c0df608de904c920ac29f72bd");

		T_OK("kpers.salt.build",
		     aliro_salt_build(ALIRO_SALT_KPERSISTENT, f_txid, rgik_x, reph_x, f_rid,
				      ALIRO_IFACE_NFC, 0x0100, 0x00, 0x01, s3opt, a5, (size_t)a5n,
				      fsalt, &fsalt_len) == 0);
		aliro_crypto_derive_key32(kdh, fsalt, fsalt_len, ceph_x, kpers);
		chk("kpersistent", kpers, 32,
		    "dd309b1738bcec549f4d6e73c6d15bf595f783d729c8ac0fa7a76ec6c8821a2d");
	}
	{
		/* §14.4: fast block = derive_block(IKM=Kpersistent, salt=salt_fast,
		 * info=credential_ephemeral_pub_x), then cryptogram verify. Fast AUTH0 flag
		 * 0x0101 (command_parameters 0x01 || auth_policy 0x01). */
		uint8_t kpers[32], rgik_x[32], reph_x[32], f_rid[32], s3opt[32], a5[16];
		uint8_t f_txid[16], ceph_x[32], fsalt[ALIRO_SALT_MAX], fblk[160];
		size_t fsalt_len = 0;
		int a5n = uh(a5, "a508800200005c020100");

		uh(kpers, "dd309b1738bcec549f4d6e73c6d15bf595f783d729c8ac0fa7a76ec6c8821a2d");
		uh(rgik_x, "b62d9b8f494f2f43a07a7db7e965865d04feeabe4e9c3b8a2f5a544ee2a9c60f");
		uh(reph_x, "de8639f30ff8c502559db84059dbc7fde720044a7ed8717eddf0481315313ed3");
		uh(f_rid, "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100");
		uh(s3opt, "88f6f8f2f1e35a58879e72d9ea81957e8964c3d3c566eb9d41c83d0d8c63a230");
		uh(f_txid, "2701e4fe10d21e15b216c550b0c5ee68");
		uh(ceph_x, "507806c74a52a8e9b34d0796e4e2382ab6f9d9d7417179fc338429bda1c2fff9");

		T_OK("fast.salt.build",
		     aliro_salt_build(ALIRO_SALT_CRYPTOGRAM, f_txid, rgik_x, reph_x, f_rid,
				      ALIRO_IFACE_NFC, 0x0100, 0x01, 0x01, s3opt, a5, (size_t)a5n,
				      fsalt, &fsalt_len) == 0);
		chk("fast.salt", fsalt, fsalt_len,
		    "b62d9b8f494f2f43a07a7db7e965865d04feeabe4e9c3b8a2f5a544ee2a9c60f"
		    "566f6c6174696c654661737400112233445566778899aabbccddeeffffeeddcc"
		    "bbaa998877665544332211005e5c020100de8639f30ff8c502559db84059dbc7"
		    "fde720044a7ed8717eddf0481315313ed32701e4fe10d21e15b216c550b0c5ee"
		    "680101a508800200005c02010088f6f8f2f1e35a58879e72d9ea81957e8964c3"
		    "d3c566eb9d41c83d0d8c63a230");

		aliro_crypto_derive_block(kpers, fsalt, fsalt_len, ceph_x, fblk);
		chk("fast.CryptogramSK", fblk + ALIRO_CRYPTOGRAM_SK_OFFSET, 32,
		    "46b35933b497ead9d72e024b267ce1db9a59ba54fc73d46bda3149a8b047bcaf");
		chk("fast.ExpeditedSKReader", fblk + 32, 32,
		    "e1010bdbdc2acf8e9ca3a31680439995aca6261500e870eb349b24ab909b1982");
		chk("fast.ExpeditedSKDevice", fblk + 64, 32,
		    "aa3d35bf0b073b1321404fc49c4d0fd8a31828f13f4d2fa27da3290796807666");
		chk("fast.BleSK", fblk + ALIRO_BLESK_OFFSET, 32,
		    "576603533baa95bbcf91ceee39dfc8b07e1d09b0eefbf2b7d10648cf038e4563");
		chk("fast.URSK", fblk + ALIRO_URSK_OFFSET, 32,
		    "c967a070ea1c609352632cfaca5ed0bd20ee554226163bc27fe0075313d9f8fe");

		/* The fast block uses the without-C layout, so split(block,0) yields the
		 * fast secure-channel keys ExpeditedSKReader (enc) / ExpeditedSKDevice (dec). */
		uint8_t fe[32], fd[32], fu[32];

		aliro_crypto_split(fblk, 0, fe, fd, fu);
		T_OK("fast.split=ExpeditedSK",
		     memcmp(fe, fblk + 32, 32) == 0 && memcmp(fd, fblk + 64, 32) == 0);

		/* §8.3.1.11: verify the cryptogram under CryptogramSK (IV=0, no AAD). */
		uint8_t csk[32], crg[ALIRO_CRYPTOGRAM_LEN], pt[ALIRO_CRYPTOGRAM_LEN];

		uh(csk, "46b35933b497ead9d72e024b267ce1db9a59ba54fc73d46bda3149a8b047bcaf");
		uh(crg, "ba76234a1e427f9e463106251fb9e9edc5f5812f59fd887d4e57eb0bc544b7cb"
			"9d368c4dedadf782d520a91f9666b9091e0973894522c04b142f6447b596942a");
		T_OK("cryptogram.verify",
		     aliro_crypto_verify_cryptogram(csk, crg, ALIRO_CRYPTOGRAM_LEN, pt) == 0);
		chk("cryptogram.plaintext", pt, ALIRO_CRYPTOGRAM_LEN - ALIRO_GCM_TAG_LEN,
		    "5e02003f911400000000000000000000000000000000000000009214000000"
		    "0000000000000000000000000000000000");
		csk[0] ^= 1;
		T_OK("cryptogram.wrong-key-rejected",
		     aliro_crypto_verify_cryptogram(csk, crg, ALIRO_CRYPTOGRAM_LEN, pt) < 0);
	}

	/* BLE advertisement Dynamic Tag (Aliro 1.0 sect. 11.3.1 / sect. 20 examples).
	 * First pin the host AES-128 double itself (FIPS-197 appendix C.1), then the
	 * derivation layout against all three spec worked examples (same expiry
	 * 0x7a4b8500), then the no-clock form as a pinned self-consistency vector
	 * (expected bytes recomputed with an independent AES implementation). */
	{
		uint8_t k16[16], adva[6], tag[ALIRO_ADVTAG_LEN];

		uh(k16, "000102030405060708090a0b0c0d0e0f");
		uh(msg, "00112233445566778899aabbccddeeff");
		T_OK("aes128.ecb", aliro_aes128_ecb_encrypt(k16, msg, out) == 0);
		chk("aes128.fips197-c1", out, 16, "69c4e0d86a7b0430d8cdb78070b4c55a");

		uh(k16, "f5b165224a58b791df6af1d8303e61cd");
		uh(adva, "c4bb86c32710");
		T_OK("advtag.derive", aliro_advtag_derive(k16, adva, 0x7a4b8500u, tag) == 0);
		chk("advtag.spec20-1", tag, sizeof(tag), "7b7f4a82557990");

		uh(k16, "3c344c4189eb2f1e7bd5d47e446fcec2");
		uh(adva, "a3d81173e578");
		T_OK("advtag.derive-2", aliro_advtag_derive(k16, adva, 0x7a4b8500u, tag) == 0);
		chk("advtag.spec20-2", tag, sizeof(tag), "ef67e4681a7783");

		uh(k16, "1bcccea696762e6116c6e9c92d99bf35");
		uh(adva, "8c2e0718e47c");
		T_OK("advtag.derive-3", aliro_advtag_derive(k16, adva, 0x7a4b8500u, tag) == 0);
		chk("advtag.spec20-3", tag, sizeof(tag), "d4dd12a45037ba");

		/* No-clock form: expiry = 0xFFFFFFFF with the sect. 20-1 key/AdvA. Not a
		 * spec vector; pinned from an independent AES recompute. */
		uh(k16, "f5b165224a58b791df6af1d8303e61cd");
		uh(adva, "c4bb86c32710");
		T_OK("advtag.derive-noclk",
		     aliro_advtag_derive(k16, adva, ALIRO_ADVTAG_EXPIRY_UNAVAILABLE, tag) == 0);
		chk("advtag.no-clock", tag, sizeof(tag), "1bee7962570be1");
	}

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
