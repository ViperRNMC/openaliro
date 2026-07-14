/** @file ccc_shim_wrap.c — per-frame STS interception (ld --wrap=dwt_configurestsiv) substituting CCC STS for the FiRa MAC; target only. */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <deca_device_api.h>

#include "ccc_shim.h"

LOG_MODULE_REGISTER(ccc_shim, LOG_LEVEL_INF);

/** @brief Log the first N intercepted STS IVs — confirms the blob index layout. */
#define CCC_SHIM_LOG_FRAMES 8

/* DW3000 STS key/IV register file IDs (dw3000_deca_regs.h); read back to prove the CCC key/IV reach the registers. */
#define STS_KEY0_REG 0x2000CUL
#define STS_KEY1_REG 0x20010UL
#define STS_KEY2_REG 0x20014UL
#define STS_KEY3_REG 0x20018UL
#define STS_IV0_REG  0x2001CUL
#define STS_IV1_REG  0x20020UL
#define STS_IV2_REG  0x20024UL
#define STS_IV3_REG  0x20028UL
/* STS_CFG0.CPS_LEN (low byte) = STS length code: 7 => 64 sym, 3 => 32 sym; read back to confirm the length. */
#define STS_CFG0_REG 0x20000UL

/** @brief The real decadriver IV load, reachable past the ld `--wrap`. */
void __real_dwt_configurestsiv(dwt_sts_cp_iv_t *pStsIv);

/** @brief Count of intercepted IVs; the first @ref CCC_SHIM_LOG_FRAMES are logged. */
static uint32_t g_log_frames;

// Reset the frame logging counter to zero, used for re-enabling diagnostics after the first N frames.
void ccc_shim_wrap_log_reset(void)
{
	g_log_frames = 0u;
}

/** @brief Pack a 16-byte `dURSK` into the DW3000 STS-key register image. */
static void pack_key(dwt_sts_cp_key_t *out, const uint8_t dursk[CCC_DURSK_LEN])
{
	uint8_t rev[CCC_DURSK_LEN];

	for (size_t i = 0; i < CCC_DURSK_LEN; i++) {
		rev[i] = dursk[CCC_DURSK_LEN - 1u - i];
	}
	out->key0 = sys_get_le32(&rev[0]);
	out->key1 = sys_get_le32(&rev[4]);
	out->key2 = sys_get_le32(&rev[8]);
	out->key3 = sys_get_le32(&rev[12]);
}

/** Pack a 16-byte STS-V into the DW3000 STS-IV image (whole-16 reverse then per-word LE, same as pack_key). */
static void pack_iv(dwt_sts_cp_iv_t *out, const uint8_t sts_v[CCC_STS_V_LEN])
{
	uint8_t rev[CCC_STS_V_LEN];

	for (size_t i = 0; i < CCC_STS_V_LEN; i++) {
		rev[i] = sts_v[CCC_STS_V_LEN - 1u - i];
	}
	out->iv0 = sys_get_le32(&rev[0]);
	out->iv1 = sys_get_le32(&rev[4]);
	out->iv2 = sys_get_le32(&rev[8]);
	out->iv3 = sys_get_le32(&rev[12]);
}

// Intercept a DW3000 STS IV configuration, mapping the FiRa blob index to CCC index space, deriving the dURSK and STS-V, and configuring the radio with CCC secrets. Logs key and IV register contents if tracing is enabled. Falls through to the real dwt_configurestsiv if shim is inactive.
void __wrap_dwt_configurestsiv(dwt_sts_cp_iv_t *pStsIv)
{
	if (ccc_shim_active() && pStsIv != NULL) {
		uint8_t dursk[CCC_DURSK_LEN], sts_v[CCC_STS_V_LEN];
		uint32_t block, sub;
		/* The {iv0..iv3} struct is the 16-byte register image the index was packed into; map the blob index into CCC-index space. */
		uint32_t blob_idx = ccc_shim_index_from_iv((const uint8_t *)pStsIv);
		uint32_t idx = ccc_shim_blob_to_ccc_index(blob_idx, &block, &sub);

		if (g_log_frames < CCC_SHIM_LOG_FRAMES) {
			LOG_INF("ccc_shim iv#%u blob=0x%08x blk=%u sub=%u -> ccc=0x%08x",
				(unsigned)g_log_frames, blob_idx, (unsigned)block,
				(unsigned)sub, idx);
		}
		g_log_frames++;

		if (ccc_shim_sts_for_index(idx, dursk, sts_v) == 0) {
			dwt_sts_cp_key_t k;
			dwt_sts_cp_iv_t v;

			pack_key(&k, dursk);
			pack_iv(&v, sts_v);
			dwt_configurestskey(&k); /* override the blob's FiRa key */
			__real_dwt_configurestsiv(&v); /* load the CCC STS-V */
			if (g_log_frames <= CCC_SHIM_LOG_FRAMES) {
				/* wr = what we programmed, rd = live register; a mismatch means the blob clobbers our STS before TX. */
				LOG_INF("ccc_shim KEY wr %08x %08x %08x %08x rd %08x %08x %08x %08x",
					k.key0, k.key1, k.key2, k.key3,
					dwt_read_reg(STS_KEY0_REG), dwt_read_reg(STS_KEY1_REG),
					dwt_read_reg(STS_KEY2_REG), dwt_read_reg(STS_KEY3_REG));
				LOG_INF("ccc_shim IV  wr %08x %08x %08x %08x rd %08x %08x %08x %08x cfg0 %08x",
					v.iv0, v.iv1, v.iv2, v.iv3,
					dwt_read_reg(STS_IV0_REG), dwt_read_reg(STS_IV1_REG),
					dwt_read_reg(STS_IV2_REG), dwt_read_reg(STS_IV3_REG),
					dwt_read_reg(STS_CFG0_REG));
			}
			return;
		}
	}

	__real_dwt_configurestsiv(pStsIv);
}
