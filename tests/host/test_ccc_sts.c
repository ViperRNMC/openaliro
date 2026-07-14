/** @file test_ccc_sts.c — DW3000 STS key/IV register pack (ccc_sts_apply). */
#include <errno.h>

#include "ccc_kdf.h"       /* CCC_DURSK_LEN, CCC_STS_V_LEN */
#include "ccc_sts.h"
#include "deca_device_api.h" /* woz_host_last_sts_key/iv capture (host shim) */
#include "test.h"

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

void test_ccc_sts(void)
{
	uint8_t dursk[CCC_DURSK_LEN];
	uint8_t sts_v[CCC_STS_V_LEN];
	uint8_t rev[CCC_DURSK_LEN];
	unsigned int calls0;

	for (size_t i = 0; i < CCC_DURSK_LEN; i++) {
		dursk[i] = (uint8_t)(0x10u + i);
		sts_v[i] = (uint8_t)(0xA0u + i);
	}

	t_group("STS key/IV register pack");
	calls0 = woz_host_sts_loadiv_calls;
	T_EQ("sts.apply", ccc_sts_apply(dursk, sts_v), 0);

	/* STS_KEY: dURSK reversed whole, then per-word little-endian. */
	for (size_t i = 0; i < CCC_DURSK_LEN; i++) {
		rev[i] = dursk[CCC_DURSK_LEN - 1u - i];
	}
	T_EQ("key0", woz_host_last_sts_key.key0, le32(&rev[0]));
	T_EQ("key1", woz_host_last_sts_key.key1, le32(&rev[4]));
	T_EQ("key2", woz_host_last_sts_key.key2, le32(&rev[8]));
	T_EQ("key3", woz_host_last_sts_key.key3, le32(&rev[12]));

	/* STS_IV: STS-V per-word little-endian, no reverse. */
	T_EQ("iv0", woz_host_last_sts_iv.iv0, le32(&sts_v[0]));
	T_EQ("iv1", woz_host_last_sts_iv.iv1, le32(&sts_v[4]));
	T_EQ("iv2", woz_host_last_sts_iv.iv2, le32(&sts_v[8]));
	T_EQ("iv3", woz_host_last_sts_iv.iv3, le32(&sts_v[12]));

	/* The IV must be latched into the STS counter exactly once per apply. */
	T_EQ("loadiv", woz_host_sts_loadiv_calls, calls0 + 1u);

	t_group("STS errors");
	T_EQ("sts.null.dursk", ccc_sts_apply(NULL, sts_v), -EINVAL);
	T_EQ("sts.null.stsv", ccc_sts_apply(dursk, NULL), -EINVAL);
}
