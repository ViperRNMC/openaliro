/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host KAT for the Aliro wire codec (aliro_apdu): AUTH0/AUTH1 command bytes, the
 * ECDSA transcript, EXCHANGE 0x98 trigger, the L2CAP envelope, and response
 * parsing. Verifies the exact recovered tag/length structure; full-command bytes
 * are behavior-locked. Pure host build, no crypto, no hardware.
 */
#include <stdio.h>
#include <string.h>

#include "aliro_apdu.h"

static int fails, pending;

static void hx(char *d, const uint8_t *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		sprintf(d + 2 * i, "%02x", b[i]);
	}
}

static void okc(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

static void veq(const char *name, const uint8_t *got, size_t n, const char *want)
{
	char g[1024];

	hx(g, got, n);
	if (want == NULL || *want == '\0') {
		printf("  REC  %-18s %s\n", name, g);
		pending++;
	} else if (strcmp(g, want) != 0) {
		printf("  FAIL %-18s\n    got  %s\n    want %s\n", name, g, want);
		fails++;
	} else {
		printf("  ok   %-18s %s\n", name, g);
	}
}

int main(void)
{
	uint8_t pub[65], pubx[32], txid[16], rid[32], out[256];
	size_t n = 0;

	for (int i = 0; i < 65; i++) {
		pub[i] = (uint8_t)(0x40 + i);
	}
	for (int i = 0; i < 32; i++) {
		pubx[i] = (uint8_t)(0x80 + i);
		rid[i] = (uint8_t)(0x10 + i);
	}
	for (int i = 0; i < 16; i++) {
		txid[i] = (uint8_t)(0xF0 - i);
	}

	printf("== AUTH0 command ==\n");
	okc("build.ok", aliro_apdu_build_auth0(0x02, 0x00, 0x0100, pub, txid, rid, out,
					       sizeof(out), &n) == 0);
	okc("len==129", n == 129);
	okc("tlv.exp_phase", out[0] == 0x41 && out[1] == 0x01 && out[2] == 0x02);
	okc("tlv.user_pol", out[3] == 0x42 && out[4] == 0x01);
	okc("tlv.version", out[6] == 0x5C && out[7] == 0x02 && out[8] == 0x01 &&
				   out[9] == 0x00);
	okc("tlv.reader_eph", out[10] == 0x87 && out[11] == 0x41); /* 0x41 = 65 */
	okc("tlv.txid", out[77] == 0x4C && out[78] == 0x10);
	okc("tlv.reader_id", out[95] == 0x4D && out[96] == 0x20);
	veq("auth0.golden", out, n,
	    "4101024201005c0201008741404142434445464748494a4b4c4d4e4f5051525354"
	    "55565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f707172737475"
	    "767778797a7b7c7d7e7f804c10f0efeeedecebeae9e8e7e6e5e4e3e2e14d201011"
	    "12131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f");

	printf("\n== AUTH1 command ==\n");
	uint8_t sig[64];

	for (int i = 0; i < 64; i++) {
		sig[i] = (uint8_t)(0xC0 + i);
	}
	okc("build.ok", aliro_apdu_build_auth1(0x01, sig, out, sizeof(out), &n) == 0);
	okc("len==69", n == 69); /* 41 01 xx + 9E 40 <64> */
	okc("tlv.cred", out[0] == 0x41 && out[1] == 0x01 && out[2] == 0x01);
	okc("tlv.sig", out[3] == 0x9E && out[4] == 0x40);

	printf("\n== ECDSA transcript (AUTH1AuthenticationData) ==\n");
	okc("reader.ok", aliro_apdu_build_authdata(ALIRO_AUTH_READER, rid, pubx, pubx,
						   txid, out, sizeof(out), &n) == 0);
	okc("len==126", n == 126);
	okc("order.rid", out[0] == 0x4D && out[1] == 0x20);
	okc("order.devx", out[34] == 0x86 && out[35] == 0x20);
	okc("order.rephx", out[68] == 0x87 && out[69] == 0x20);
	okc("order.txid", out[102] == 0x4C && out[103] == 0x10);
	/* reader usage domain separator = 93 04 41 5D 95 69 */
	veq("reader.usage", out + n - 6, 6, "9304415d9569");
	aliro_apdu_build_authdata(ALIRO_AUTH_DEVICE, rid, pubx, pubx, txid, out,
				  sizeof(out), &n);
	veq("device.usage", out + n - 6, 6, "93044e887b4c");

	printf("\n== EXCHANGE + 0x98 trigger ==\n");
	okc("ursk-ready.ok",
	    aliro_apdu_build_exchange(0, 0, 1, out, sizeof(out), &n) == 0);
	veq("ursk-ready.bytes", out, n, "9800");
	aliro_apdu_build_exchange(1, 0x0005, 1, out, sizeof(out), &n);
	veq("status+ready.bytes", out, n, "970200059800");

	printf("\n== AUTH0 standard path: 1-byte phase (41 01 00) + APDU wrap + SW strip ==\n");
	okc("build.phase0", aliro_apdu_build_auth0(0x00, 0x01, 0x0100, pub, txid, rid, out,
						   sizeof(out), &n) == 0);
	okc("phase.1byte", out[0] == 0x41 && out[1] == 0x01 && out[2] == 0x00); /* 41 01 00, not empty */
	okc("user_pol.01", out[3] == 0x42 && out[4] == 0x01 && out[5] == 0x01);
	okc("len==129", n == 129);
	/* fast-phase request = command_parameters bit 0 (41 01 01) */
	okc("build.fast", aliro_apdu_build_auth0(0x01, 0x01, 0x0100, pub, txid, rid, out,
						 sizeof(out), &n) == 0);
	okc("phase.fastbit", out[0] == 0x41 && out[1] == 0x01 && out[2] == 0x01);
	{
		uint8_t apdu[300];
		size_t alen;

		okc("wrap.ok", aliro_apdu_wrap(ALIRO_INS_AUTH0, out, n, apdu, sizeof(apdu),
					       &alen) == 0);
		okc("wrap.hdr", apdu[0] == 0x80 && apdu[1] == 0x80 && apdu[2] == 0x00 &&
					apdu[3] == 0x00 && apdu[4] == (uint8_t)n); /* Lc = 0x81 */
		okc("wrap.le0", apdu[5 + n] == 0x00);   /* Le */
		okc("wrap.len", alen == n + 6);         /* CLA INS P1 P2 Lc <n> Le */
	}
	{
		uint8_t rb[4] = { 0x86, 0x00, 0x90, 0x00 }; /* <tlv> SW=9000 */
		size_t rl = sizeof(rb);
		uint16_t sw = 0;

		okc("strip.sw9000", aliro_apdu_strip_sw(rb, &rl, &sw) == 0 && rl == 2 &&
					    sw == 0x9000);
		size_t one = 1;

		okc("strip.tooshort", aliro_apdu_strip_sw(rb, &one, &sw) < 0);
	}

	printf("\n== L2CAP envelope ==\n");
	uint8_t pl[3] = { 0xAA, 0xBB, 0xCC };

	aliro_ble_frame(0x00, ALIRO_INS_AUTH0, pl, 3, out, sizeof(out), &n);
	/* type 0x00, opcode byte 0x80, len BE 0x0003, payload aabbcc */
	veq("frame.exact", out, n, "00800003aabbcc");
	uint8_t ty, op;
	const uint8_t *pp;
	size_t ppl;

	okc("unframe.ok",
	    aliro_ble_unframe(out, n, &ty, &op, &pp, &ppl) == 0 && ty == 0 &&
		    op == 0x80 && ppl == 3 && memcmp(pp, pl, 3) == 0);

	printf("\n== response parsers ==\n");
	/* AUTH0Response: 86 41 <65> 9D 40 <64> */
	struct aliro_tlv_w w;
	uint8_t rbuf[256];
	size_t rlen;

	aliro_tlv_w_init(&w, rbuf, sizeof(rbuf));
	aliro_tlv_put(&w, 0x86, pub, 65);
	aliro_tlv_put(&w, 0x9D, sig, 64);
	aliro_tlv_w_finish(&w, &rlen);
	struct aliro_auth0_response a0;

	okc("auth0resp.parse", aliro_apdu_parse_auth0_response(rbuf, rlen, &a0) == 0);
	okc("auth0resp.pub", memcmp(a0.device_eph_pub, pub, 65) == 0);
	okc("auth0resp.crypto", a0.have_cryptogram &&
					memcmp(a0.cryptogram, sig, 64) == 0);
	/* standard response (no 0x9D): parses with have_cryptogram unset */
	aliro_tlv_w_init(&w, rbuf, sizeof(rbuf));
	aliro_tlv_put(&w, 0x86, pub, 65);
	aliro_tlv_w_finish(&w, &rlen);
	okc("auth0resp.no-crypto",
	    aliro_apdu_parse_auth0_response(rbuf, rlen, &a0) == 0 && a0.have_cryptogram == 0);
	/* missing mandatory 0x86 -> reject */
	aliro_tlv_w_init(&w, rbuf, sizeof(rbuf));
	aliro_tlv_put(&w, 0x9D, sig, 64);
	aliro_tlv_w_finish(&w, &rlen);
	okc("auth0resp.reject-no86",
	    aliro_apdu_parse_auth0_response(rbuf, rlen, &a0) < 0);

	/* AUTH1Response: 5A 41 <65> 9E 40 <64> */
	aliro_tlv_w_init(&w, rbuf, sizeof(rbuf));
	aliro_tlv_put(&w, 0x5A, pub, 65);
	aliro_tlv_put(&w, 0x9E, sig, 64);
	aliro_tlv_w_finish(&w, &rlen);
	struct aliro_auth1_response a1;

	okc("auth1resp.parse", aliro_apdu_parse_auth1_response(rbuf, rlen, &a1) == 0);
	okc("auth1resp.sig", memcmp(a1.device_sig, sig, 64) == 0);
	okc("auth1resp.pub", a1.have_device_pub &&
				     memcmp(a1.device_pub, pub, 65) == 0);

	printf("\n== BER long-form length ==\n");
	uint8_t big[200], bigout[256];

	memset(big, 0x5a, sizeof(big));
	aliro_tlv_w_init(&w, bigout, sizeof(bigout));
	aliro_tlv_put(&w, 0xB2, big, 200);
	aliro_tlv_w_finish(&w, &rlen);
	okc("longform.hdr", bigout[0] == 0xB2 && bigout[1] == 0x81 && bigout[2] == 200);
	const uint8_t *fv;
	size_t fl;

	okc("longform.find", aliro_tlv_find(bigout, rlen, 0xB2, &fv, &fl) == 0 &&
				     fl == 200 && memcmp(fv, big, 200) == 0);

	if (fails) {
		printf("\nRESULT: %d FAIL\n", fails);
		return 1;
	}
	if (pending) {
		printf("\nRESULT: RECORD (%d to bake)\n", pending);
		return 2;
	}
	printf("\nRESULT: PASS\n");
	return 0;
}
