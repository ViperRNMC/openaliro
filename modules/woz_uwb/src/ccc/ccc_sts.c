/** @file ccc_sts.c — DW3000 STS register load for the CCC ranging path. */

#include "ccc_sts.h"

#include <errno.h>

#include <zephyr/sys/byteorder.h>

#include <deca_device_api.h>

int ccc_sts_apply(const uint8_t dursk[CCC_DURSK_LEN],
		  const uint8_t sts_v[CCC_STS_V_LEN])
{
	uint8_t rev_key[CCC_DURSK_LEN];
	dwt_sts_cp_key_t key;
	dwt_sts_cp_iv_t iv;

	if (dursk == NULL || sts_v == NULL) {
		return -EINVAL;
	}

	/* STS_KEY (= dURSK): reverse the whole 16-byte key, then per-word LE pack. */
	for (size_t i = 0; i < CCC_DURSK_LEN; i++) {
		rev_key[i] = dursk[CCC_DURSK_LEN - 1u - i];
	}
	key.key0 = sys_get_le32(&rev_key[0]);
	key.key1 = sys_get_le32(&rev_key[4]);
	key.key2 = sys_get_le32(&rev_key[8]);
	key.key3 = sys_get_le32(&rev_key[12]);
	dwt_configurestskey(&key);

	/* STS_IV (= STS-V): per-word LE packing, no whole-array reverse. */
	iv.iv0 = sys_get_le32(&sts_v[0]);
	iv.iv1 = sys_get_le32(&sts_v[4]);
	iv.iv2 = sys_get_le32(&sts_v[8]);
	iv.iv3 = sys_get_le32(&sts_v[12]);
	dwt_configurestsiv(&iv);

	/* Latch the IV into the STS counter for the next PPDU. */
	dwt_configurestsloadiv();

	return 0;
}
