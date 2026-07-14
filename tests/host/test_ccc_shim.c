/** @file test_ccc_shim.c — STS substitution core: bind, per-index mapping, blob calibration. */
#include <errno.h>
#include <string.h>

#include "ccc_kdf.h"
#include "ccc_shim.h"
#include "test.h"

static const uint8_t URSK[CCC_URSK_LEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
static const uint8_t RC[16] = {
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
};
#define STS0     0x1000u
#define NSLOT    6u

/* Independent reference: reproduce the shim's derivation via the public KDF. */
static void ref_sts(uint32_t index, uint8_t dursk[CCC_DURSK_LEN],
		    uint8_t sts_v[CCC_STS_V_LEN], uint8_t dudsk[CCC_DUDSK_LEN])
{
	uint8_t mursk[CCC_MURSK_LEN], salted[CCC_SALTED_HASH_LEN];
	uint8_t ursk_kt[CCC_URSK_KT_LEN];
	uint32_t base = index - ((index - STS0) % NSLOT);

	ccc_derive_mursk(URSK, mursk);
	ccc_derive_salted_hash(URSK, RC, sizeof(RC), salted);
	ccc_derive_ursk_kt(mursk, base, ursk_kt);
	if (dursk) {
		ccc_derive_dursk(ursk_kt, salted, dursk);
	}
	if (sts_v) {
		ccc_derive_sts_v(salted, index, sts_v);
	}
	if (dudsk) {
		ccc_derive_dudsk(ursk_kt, salted, dudsk);
	}
}

void test_ccc_shim(void)
{
	uint8_t dursk[CCC_DURSK_LEN], sts_v[CCC_STS_V_LEN], dudsk[CCC_DUDSK_LEN];
	uint8_t edursk[CCC_DURSK_LEN], estsv[CCC_STS_V_LEN], edudsk[CCC_DUDSK_LEN];

	t_group("bind + active");
	ccc_shim_unbind();
	T_OK("inactive.before", !ccc_shim_active());
	T_EQ("bind", ccc_shim_bind_from_ursk(URSK, RC, sizeof(RC), STS0, NSLOT), 0);
	T_OK("active.after", ccc_shim_active());
	T_EQ("sts_index0", ccc_shim_sts_index0(), STS0);

	t_group("sts_for_index matches the KDF reference");
	T_EQ("sts.idx", ccc_shim_sts_for_index(STS0 + 2u, dursk, sts_v), 0);
	ref_sts(STS0 + 2u, edursk, estsv, NULL);
	T_OK("dursk.match", memcmp(dursk, edursk, sizeof(dursk)) == 0);
	T_OK("sts_v.match", memcmp(sts_v, estsv, sizeof(sts_v)) == 0);

	/* Same cycle -> cached dURSK identical, STS-V advances with the index. */
	uint8_t dursk2[CCC_DURSK_LEN], sts_v2[CCC_STS_V_LEN];
	T_EQ("sts.samecycle", ccc_shim_sts_for_index(STS0 + 3u, dursk2, sts_v2), 0);
	T_OK("dursk.cached", memcmp(dursk2, dursk, sizeof(dursk)) == 0);
	T_OK("sts_v.advanced", memcmp(sts_v2, sts_v, sizeof(sts_v)) != 0);

	/* Next cycle -> dURSK changes. */
	T_EQ("sts.nextcycle", ccc_shim_sts_for_index(STS0 + NSLOT, dursk2, sts_v2), 0);
	ref_sts(STS0 + NSLOT, edursk, NULL, NULL);
	T_OK("dursk.recomputed", memcmp(dursk2, edursk, sizeof(dursk2)) == 0);

	t_group("dudsk + sts_for_slot");
	T_EQ("dudsk", ccc_shim_dudsk_for_index(STS0 + 2u, dudsk), 0);
	ref_sts(STS0 + 2u, NULL, NULL, edudsk);
	T_OK("dudsk.match", memcmp(dudsk, edudsk, sizeof(dudsk)) == 0);
	T_EQ("slot", ccc_shim_sts_for_slot(2u, dursk2, sts_v2), 0);
	ccc_shim_sts_for_index(STS0 + 2u, dursk, sts_v);
	T_OK("slot.eq.index", memcmp(dursk2, dursk, sizeof(dursk)) == 0 &&
				     memcmp(sts_v2, sts_v, sizeof(sts_v)) == 0);

	t_group("suspend gates active()");
	ccc_shim_suspend(true);
	T_OK("suspended.inactive", !ccc_shim_active());
	ccc_shim_suspend(false);
	T_OK("resumed.active", ccc_shim_active());

	t_group("index_from_iv");
	uint8_t iv[16] = { 0 };
	iv[4] = 0x11; iv[5] = 0x22; iv[6] = 0x33; iv[7] = 0x44;
	T_EQ("iv.index", ccc_shim_index_from_iv(iv), 0x44332211u);

	t_group("blob-index calibration");
	ccc_shim_bind_from_ursk(URSK, RC, sizeof(RC), STS0, NSLOT); /* resets calib */
	uint32_t blk = 99u, sub = 99u;
	/* 1st frame: learns origin, stride unknown -> block 0, sub 0. */
	T_EQ("blob.origin", ccc_shim_blob_to_ccc_index(100u, &blk, &sub), STS0);
	T_EQ("blob.origin.blk", blk, 0u);
	T_EQ("blob.origin.sub", sub, 0u);
	/* 2nd frame: learns stride=10; rel=10 -> block 1, sub 0. */
	T_EQ("blob.stride", ccc_shim_blob_to_ccc_index(110u, &blk, &sub), STS0 + NSLOT);
	T_EQ("blob.stride.blk", blk, 1u);
	/* 3rd frame within block 1: rel=15 -> block 1, sub 5. */
	T_EQ("blob.sub", ccc_shim_blob_to_ccc_index(115u, &blk, &sub),
	     STS0 + NSLOT + 5u);
	T_EQ("blob.sub.blk", blk, 1u);
	T_EQ("blob.sub.off", sub, 5u);

	t_group("debug pin");
	ccc_shim_pin_index(0xABCDu);
	T_EQ("pinned", ccc_shim_blob_to_ccc_index(999u, NULL, NULL), 0xABCDu);
	ccc_shim_unpin();
	T_OK("unpinned", ccc_shim_blob_to_ccc_index(120u, NULL, NULL) != 0xABCDu);

	t_group("errors when unbound / bad args");
	T_EQ("bind.null.mursk", ccc_shim_bind(NULL, RC, STS0, NSLOT), -EINVAL);
	uint8_t mursk[CCC_MURSK_LEN] = { 0 }, salted[CCC_SALTED_HASH_LEN] = { 0 };
	T_EQ("bind.zero.nslot", ccc_shim_bind(mursk, salted, STS0, 0u), -EINVAL);
	T_EQ("bind_ursk.null", ccc_shim_bind_from_ursk(NULL, RC, sizeof(RC), STS0, NSLOT),
	     -EINVAL);
	ccc_shim_unbind();
	T_OK("unbound.inactive", !ccc_shim_active());
	T_EQ("unbound.sts0", ccc_shim_sts_index0(), 0u);
	T_EQ("sts.unbound", ccc_shim_sts_for_index(STS0, dursk, sts_v), -EINVAL);
	T_EQ("slot.unbound", ccc_shim_sts_for_slot(0u, dursk, sts_v), -EINVAL);
	T_EQ("dudsk.unbound", ccc_shim_dudsk_for_index(STS0, dudsk), -EINVAL);
	/* bad-arg after re-bind */
	ccc_shim_bind_from_ursk(URSK, RC, sizeof(RC), STS0, NSLOT);
	T_EQ("sts.null.out", ccc_shim_sts_for_index(STS0, NULL, sts_v), -EINVAL);
	T_EQ("dudsk.null.out", ccc_shim_dudsk_for_index(STS0, NULL), -EINVAL);
	ccc_shim_unbind();
}
