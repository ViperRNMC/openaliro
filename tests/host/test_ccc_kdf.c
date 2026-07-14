/** @file test_ccc_kdf.c — CCC key schedule + SP0 AES-CCM* + FIPS/RFC anchors. */
#include <errno.h>
#include <string.h>

#include "ccc_kdf.h"
#include "ccc_mac.h"
#include "test.h"

/* Fixed inputs. Change only if you intend to regenerate every golden. */
static const uint8_t URSK[CCC_URSK_LEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
static const uint8_t RANGING_CONFIG[16] = {
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
};
#define STS_INDEX_KT 5u
#define STS_INDEX_V  7u
#define STS_INDEX0   3u

/* Baked-in behavior-lock goldens. */
#define G_MUPSK1     "8130793f00b6f71bf39deb342347806b"
#define G_MUPSK2     "0d729b1d50b921d7fd21a2f0841fa3f176286b1b2ea626c036a7276ec6f4ad3f"
#define G_MURSK      "cb2e6fb9390768bd6b99d7547420aec7b9be2702e42093e55bf11d6e1b524b72"
#define G_SALTEDHASH "de702530285dc68ea7f138bdfa607a2d"
#define G_URSK_KT    "a25370455670465277a7721d1ac85482ad73bc021053c1a3082f6af6aadf5c54"
#define G_DURSK      "15c03bdc50dd7502835e520c81b4178b"
#define G_DUDSK      "f83633ef0bc0e60f4d492288f177a006"
#define G_STS_V      "de702530285dc68ea7f138c4fa607a2d"
#define G_UAD        "3df8a88315f276fd20ac970181aa3b27"
#define G_KEYSOURCE  "a8833df8"
#define G_DEST_SHORT "15f2"
#define G_SRC_LONG   "76fd20ac970181aa"
#define G_SP0_CT     "7464c1539de11c53a418ce30df"
#define G_SP0_MIC    "ea8c5cb70cc4c81f"

static void anchors(void)
{
	uint8_t key[32], in[16], out[16], m[64], tag[CCC_CMAC_TAG_LEN];

	t_group("FIPS-197 AES-ECB known-answer");
	t_unhex(key, "000102030405060708090a0b0c0d0e0f", sizeof(key));
	t_unhex(in, "00112233445566778899aabbccddeeff", sizeof(in));
	crypto_aes_ecb_encrypt(key, 128, in, out);
	t_vec("aes128", out, 16, "69c4e0d86a7b0430d8cdb78070b4c55a");
	t_unhex(key, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
		sizeof(key));
	crypto_aes_ecb_encrypt(key, 256, in, out);
	t_vec("aes256", out, 16, "8ea2b7ca516745bfeafc49904b496089");
	/* invalid key size -> EINVAL */
	T_EQ("aes.badbits", crypto_aes_ecb_encrypt(key, 192, in, out), -EINVAL);
	T_EQ("aes.null", crypto_aes_ecb_encrypt(NULL, 128, in, out), -EINVAL);

	t_group("RFC 4493 AES-CMAC known-answer");
	t_unhex(key, "2b7e151628aed2a6abf7158809cf4f3c", sizeof(key)); /* gitleaks:allow */
	t_unhex(m,
		"6bc1bee22e409f96e93d7e117393172a"
		"ae2d8a571e03ac9c9eb76fac45af8e51"
		"30c81c46a35ce411e5fbc1191a0a52ef"
		"f69f2445df4f9b17ad2b417be66c3710",
		sizeof(m));
	ccc_aes_cmac(key, 128, m, 0, tag);
	t_vec("cmac.len0", tag, 16, "bb1d6929e95937287fa37d129b756746");
	ccc_aes_cmac(key, 128, m, 16, tag);
	t_vec("cmac.len16", tag, 16, "070a16b46b4d4144f79bdd9dd04a287c");
	ccc_aes_cmac(key, 128, m, 40, tag);
	t_vec("cmac.len40", tag, 16, "dfa66747de9ae63030ca32611497c827");
	ccc_aes_cmac(key, 128, m, 64, tag);
	t_vec("cmac.len64", tag, 16, "51f0bebf7e3b9d92fc49741779363cfe");
	T_EQ("cmac.nullkey", ccc_aes_cmac(NULL, 128, m, 16, tag), -EINVAL);
	T_EQ("cmac.nullmsg", ccc_aes_cmac(key, 128, NULL, 16, tag), -EINVAL);
}

static void ladder(void)
{
	uint8_t mupsk1[CCC_MUPSK1_LEN], mupsk2[CCC_MUPSK2_LEN], mursk[CCC_MURSK_LEN];
	uint8_t salted[CCC_SALTED_HASH_LEN], ursk_kt[CCC_URSK_KT_LEN];
	uint8_t dursk[CCC_DURSK_LEN], dudsk[CCC_DUDSK_LEN], sts_v[CCC_STS_V_LEN];
	uint8_t uad[CCC_UAD_LEN], keysource[CCC_KEYSOURCE_LEN];
	uint8_t dest_short[CCC_DEST_SHORT_ADDR_LEN], src_long[CCC_SRC_LONG_ADDR_LEN];

	t_group("key schedule behavior-lock");
	ccc_derive_mupsk1(URSK, mupsk1);
	t_vec("mUPSK1", mupsk1, sizeof(mupsk1), G_MUPSK1);
	ccc_derive_mupsk2(URSK, mupsk2);
	t_vec("mUPSK2", mupsk2, sizeof(mupsk2), G_MUPSK2);
	ccc_derive_mursk(URSK, mursk);
	t_vec("mURSK", mursk, sizeof(mursk), G_MURSK);
	ccc_derive_salted_hash(URSK, RANGING_CONFIG, sizeof(RANGING_CONFIG), salted);
	t_vec("SaltedHash", salted, sizeof(salted), G_SALTEDHASH);
	ccc_derive_ursk_kt(mursk, STS_INDEX_KT, ursk_kt);
	t_vec("URSK_KT", ursk_kt, sizeof(ursk_kt), G_URSK_KT);
	ccc_derive_dursk(ursk_kt, salted, dursk);
	t_vec("dURSK", dursk, sizeof(dursk), G_DURSK);
	ccc_derive_dudsk(ursk_kt, salted, dudsk);
	t_vec("dUDSK", dudsk, sizeof(dudsk), G_DUDSK);
	ccc_derive_sts_v(salted, STS_INDEX_V, sts_v);
	t_vec("STS-V", sts_v, sizeof(sts_v), G_STS_V);
	ccc_derive_uad(mupsk2, STS_INDEX0, uad);
	t_vec("UAD", uad, sizeof(uad), G_UAD);
	ccc_uad_addresses(uad, keysource, dest_short, src_long);
	t_vec("KeySource", keysource, sizeof(keysource), G_KEYSOURCE);
	t_vec("DestShort", dest_short, sizeof(dest_short), G_DEST_SHORT);
	t_vec("SrcLong", src_long, sizeof(src_long), G_SRC_LONG);

	t_group("key schedule determinism + errors");
	uint8_t again[CCC_MURSK_LEN];
	ccc_derive_mursk(URSK, again);
	T_OK("mURSK.deterministic", memcmp(again, mursk, sizeof(mursk)) == 0);
	T_EQ("mupsk1.null", ccc_derive_mupsk1(NULL, mupsk1), -EINVAL);
	T_EQ("mupsk2.null", ccc_derive_mupsk2(URSK, NULL), -EINVAL);
	T_EQ("mursk.null", ccc_derive_mursk(NULL, mursk), -EINVAL);
	T_EQ("salted.null", ccc_derive_salted_hash(NULL, RANGING_CONFIG, 16, salted),
	     -EINVAL);
	T_EQ("ursk_kt.null", ccc_derive_ursk_kt(NULL, 0, ursk_kt), -EINVAL);
	T_EQ("dursk.null", ccc_derive_dursk(NULL, salted, dursk), -EINVAL);
	T_EQ("dudsk.null", ccc_derive_dudsk(ursk_kt, NULL, dudsk), -EINVAL);
	T_EQ("sts_v.null", ccc_derive_sts_v(NULL, 0, sts_v), -EINVAL);
	T_EQ("uad.null", ccc_derive_uad(NULL, 0, uad), -EINVAL);
	T_EQ("uad_addr.null", ccc_uad_addresses(NULL, keysource, dest_short, src_long),
	     -EINVAL);
}

static void uad_reserved_remap(void)
{
	/* Feed a UAD whose split addresses are the reserved all-ones values so the
	 * top-bit remap in ccc_uad_addresses fires on every field. */
	uint8_t uad[CCC_UAD_LEN];
	uint8_t keysource[CCC_KEYSOURCE_LEN];
	uint8_t dest_short[CCC_DEST_SHORT_ADDR_LEN];
	uint8_t src_long[CCC_SRC_LONG_ADDR_LEN];

	t_group("UAD reserved-address remap");
	memset(uad, 0xff, sizeof(uad));            /* all fields all-ones */
	uad[5] = 0xfe;                             /* dest short 0xfffe (reserved) */
	ccc_uad_addresses(uad, keysource, dest_short, src_long);
	/* Reserved values get their top byte's MSB cleared. */
	T_OK("ks_high.remapped", (keysource[0] & 0x80u) == 0);
	T_OK("dest_short.remapped", (dest_short[0] & 0x80u) == 0);
	T_OK("src_long.remapped", (src_long[0] & 0x80u) == 0);
}

static void sp0(void)
{
	uint8_t mupsk1[CCC_MUPSK1_LEN];
	const uint8_t src_long[CCC_SRC_LONG_ADDR_LEN] = {
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	};
	struct ccc_mhr_fields mf = {
		.dest_short_addr = 0x1234u,
		.frame_counter = 1u,
		.key_source = { 0xde, 0xad, 0xbe, 0xef },
		.msg_id = CCC_MSG_ID_PRE_POLL,
		.payload_len = CCC_PRE_POLL_LEN,
	};
	struct ccc_pre_poll pp = {
		.uwb_session_id = 0x01020304u,
		.poll_sts_index = 6u,
		.ranging_block = 2u,
		.hop_flag = 1u,
		.round_index = 3u,
	};
	uint8_t mhr[CCC_MHR_LEN], payload[CCC_PRE_POLL_LEN], ct[CCC_PRE_POLL_LEN];
	uint8_t mic[CCC_SP0_MIC_LEN], recovered[CCC_PRE_POLL_LEN];

	t_group("SP0 AES-CCM* round-trip");
	ccc_derive_mupsk1(URSK, mupsk1);
	ccc_build_mhr(&mf, mhr);
	ccc_pre_poll_pack(&pp, payload);
	T_EQ("sp0.enc", ccc_sp0_encrypt(mupsk1, src_long, 1u, mhr, sizeof(mhr),
				       payload, sizeof(payload), ct, mic), 0);
	t_vec("SP0.ct", ct, sizeof(ct), G_SP0_CT);
	t_vec("SP0.mic", mic, sizeof(mic), G_SP0_MIC);
	T_EQ("sp0.dec", ccc_sp0_decrypt(mupsk1, src_long, 1u, mhr, sizeof(mhr), ct,
					sizeof(ct), mic, recovered), 0);
	T_OK("sp0.roundtrip", memcmp(recovered, payload, sizeof(payload)) == 0);

	/* Tamper: a flipped ciphertext byte must fail the MIC. */
	ct[0] ^= 0x01u;
	T_EQ("sp0.tamper", ccc_sp0_decrypt(mupsk1, src_long, 1u, mhr, sizeof(mhr), ct,
					   sizeof(ct), mic, recovered), -EBADMSG);
	ct[0] ^= 0x01u;

	/* Multi-block payload (40 B) exercises the CTR + CBC-MAC block loops. */
	uint8_t big[40], bigct[40], bigrec[40];
	for (size_t i = 0; i < sizeof(big); i++) {
		big[i] = (uint8_t)(i * 7u + 1u);
	}
	T_EQ("sp0.enc40", ccc_sp0_encrypt(mupsk1, src_long, 2u, mhr, sizeof(mhr), big,
					  sizeof(big), bigct, mic), 0);
	T_EQ("sp0.dec40", ccc_sp0_decrypt(mupsk1, src_long, 2u, mhr, sizeof(mhr),
					  bigct, sizeof(big), mic, bigrec), 0);
	T_OK("sp0.roundtrip40", memcmp(bigrec, big, sizeof(big)) == 0);

	t_group("SP0 error paths");
	T_EQ("sp0.enc.null", ccc_sp0_encrypt(NULL, src_long, 1u, mhr, sizeof(mhr),
					     payload, sizeof(payload), ct, mic), -EINVAL);
	/* payload beyond the CCM scratch bound -> E2BIG */
	uint8_t huge[200];
	memset(huge, 0, sizeof(huge));
	T_EQ("sp0.enc.e2big", ccc_sp0_encrypt(mupsk1, src_long, 1u, mhr, sizeof(mhr),
					      huge, sizeof(huge), bigct, mic), -E2BIG);
	T_EQ("sp0.dec.null", ccc_sp0_decrypt(mupsk1, NULL, 1u, mhr, sizeof(mhr), ct,
					     sizeof(ct), mic, recovered), -EINVAL);
}

void test_ccc_kdf(void)
{
	anchors();
	ladder();
	uad_reserved_remap();
	sp0();
}
