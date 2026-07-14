/** @file ccc_shim.c — CCC STS substitution core (implementation). */

#include "ccc_shim.h"

#include <errno.h>
#include <string.h>

/** @brief The bound session's key material, schedule anchor, and dURSK cache. */
static struct {
	bool	 active;
	uint32_t sts_index0;
	uint16_t n_slot_per_round;
	uint8_t	 mursk[CCC_MURSK_LEN];
	uint8_t	 salted_hash[CCC_SALTED_HASH_LEN];
	/* One-deep dURSK cache: dURSK is per ranging cycle, reused across a round of PPDUs. */
	bool	 cache_valid;
	uint32_t cache_base;
	uint8_t	 cache_dursk[CCC_DURSK_LEN];
	/* Blob-index calibration: origin + per-block stride learned from the first two indices (calib: 0 none, 1 origin, 2 stride). */
	uint8_t	 calib;
	uint32_t blob_origin;
	uint32_t blob_stride;
	/* Debug pin: emit the STS for one fixed CCC index so a sniffer keyed once validates every POLL. */
	bool	 pinned;
	uint32_t pin_index;
	/* Suspend: keep the derivation state live but make the per-frame IV wrap pass through. */
	bool	 suspend;
} g;

/** Intra-block slot hook: map a blob sub-block offset to a CCC slot (currently pass-through). */
static uint32_t ccc_shim_slot_from_sub(uint32_t sub)
{
	return sub;
}

int ccc_shim_bind(const uint8_t mursk[CCC_MURSK_LEN],
		  const uint8_t salted_hash[CCC_SALTED_HASH_LEN],
		  uint32_t sts_index0, uint16_t n_slot_per_round)
{
	if (mursk == NULL || salted_hash == NULL || n_slot_per_round == 0u) {
		return -EINVAL;
	}
	memcpy(g.mursk, mursk, sizeof(g.mursk));
	memcpy(g.salted_hash, salted_hash, sizeof(g.salted_hash));
	g.sts_index0 = sts_index0;
	g.n_slot_per_round = n_slot_per_round;
	g.cache_valid = false;
	g.calib = 0u; /* re-learn the blob index origin/stride for this session */
	g.active = true;
	return 0;
}

int ccc_shim_bind_from_ursk(const uint8_t ursk[CCC_URSK_LEN],
			    const uint8_t *ranging_config, size_t rc_len,
			    uint32_t sts_index0, uint16_t n_slot_per_round)
{
	uint8_t mursk[CCC_MURSK_LEN];
	uint8_t salted_hash[CCC_SALTED_HASH_LEN];
	int rc;

	if (ursk == NULL) {
		return -EINVAL;
	}
	rc = ccc_derive_mursk(ursk, mursk);
	if (rc != 0) {
		return rc;
	}
	rc = ccc_derive_salted_hash(ursk, ranging_config, rc_len, salted_hash);
	if (rc != 0) {
		return rc;
	}
	return ccc_shim_bind(mursk, salted_hash, sts_index0, n_slot_per_round);
}

void ccc_shim_unbind(void)
{
	memset(&g, 0, sizeof(g));
}

bool ccc_shim_active(void)
{
	/* The per-frame IV wrap gates on this: bound AND not suspended. */
	return g.active && !g.suspend;
}

uint32_t ccc_shim_sts_index0(void)
{
	return g.sts_index0;
}

void ccc_shim_suspend(bool suspend)
{
	g.suspend = suspend;
}

int ccc_shim_sts_for_index(uint32_t sts_index, uint8_t dursk[CCC_DURSK_LEN],
			   uint8_t sts_v[CCC_STS_V_LEN])
{
	uint32_t base;
	int rc;

	if (!g.active || dursk == NULL || sts_v == NULL) {
		return -EINVAL;
	}

	/* Strip the slot offset to the cycle base: (sts_index - STS_Index0) mod N_Slot_per_Round is the slot offset. */
	base = sts_index - ((sts_index - g.sts_index0) % g.n_slot_per_round);

	/* dURSK is per ranging cycle; recompute only when the cycle changes. */
	if (!g.cache_valid || g.cache_base != base) {
		uint8_t ursk_kt[CCC_URSK_KT_LEN];

		rc = ccc_derive_ursk_kt(g.mursk, base, ursk_kt);
		if (rc != 0) {
			return rc;
		}
		rc = ccc_derive_dursk(ursk_kt, g.salted_hash, g.cache_dursk);
		if (rc != 0) {
			return rc;
		}
		g.cache_base = base;
		g.cache_valid = true;
	}
	memcpy(dursk, g.cache_dursk, CCC_DURSK_LEN);

	return ccc_derive_sts_v(g.salted_hash, sts_index, sts_v);
}

int ccc_shim_dudsk_for_index(uint32_t sts_index, uint8_t dudsk[CCC_DUDSK_LEN])
{
	uint32_t base;
	uint8_t ursk_kt[CCC_URSK_KT_LEN];
	int rc;

	if (!g.active || dudsk == NULL) {
		return -EINVAL;
	}
	/* Same per-cycle base as the STS path; dUDSK shares URSK_KT, differs only by the KDF label. Derived fresh (no cache). */
	base = sts_index - ((sts_index - g.sts_index0) % g.n_slot_per_round);
	rc = ccc_derive_ursk_kt(g.mursk, base, ursk_kt);
	if (rc != 0) {
		return rc;
	}
	return ccc_derive_dudsk(ursk_kt, g.salted_hash, dudsk);
}

int ccc_shim_sts_for_slot(uint32_t slot, uint8_t dursk[CCC_DURSK_LEN],
			  uint8_t sts_v[CCC_STS_V_LEN])
{
	if (!g.active) {
		return -EINVAL;
	}
	/* Absolute index = STS_Index0 + slot; ccc_shim_sts_for_index strips it to the per-cycle base. */
	return ccc_shim_sts_for_index(g.sts_index0 + slot, dursk, sts_v);
}

uint32_t ccc_shim_index_from_iv(const uint8_t iv16[16])
{
	/* Bench-confirmed layout (2026-07-06) — see ccc_shim.h. */
	return ((uint32_t)iv16[7] << 24) | ((uint32_t)iv16[6] << 16) |
	       ((uint32_t)iv16[5] << 8) | (uint32_t)iv16[4];
}

uint32_t ccc_shim_blob_to_ccc_index(uint32_t blob_idx, uint32_t *block,
				    uint32_t *sub)
{
	uint32_t rel, blk, off;

	/* Auto-calibrate the blob's index origin (1st frame) then per-block stride (2nd); a zero delta holds at calib=1. */
	if (g.calib == 0u) {
		g.blob_origin = blob_idx;
		g.calib = 1u;
	} else if (g.calib == 1u && blob_idx != g.blob_origin) {
		g.blob_stride = blob_idx - g.blob_origin;
		g.calib = 2u;
	}

	rel = blob_idx - g.blob_origin; /* uint32 wraps with the STS-index space */
	if (g.blob_stride != 0u) {
		blk = rel / g.blob_stride;
		off = rel % g.blob_stride;
	} else {
		/* Stride not learned yet (frames 0..1): treat as block 0. */
		blk = 0u;
		off = rel;
	}

	if (block != NULL) {
		*block = blk;
	}
	if (sub != NULL) {
		*sub = off;
	}

	/* Debug pin: emit a constant STS regardless of the blob index; block/sub still reflect the real advance for the log. */
	if (g.pinned) {
		return g.pin_index;
	}

	/* Re-express in CCC-index space; per-block CCC stride defaults to N_Slot_per_Round, slot hook places the frame in the round. */
	return g.sts_index0 + blk * (uint32_t)g.n_slot_per_round +
	       ccc_shim_slot_from_sub(off);
}

void ccc_shim_pin_index(uint32_t ccc_index)
{
	g.pin_index = ccc_index;
	g.pinned = true;
}

void ccc_shim_unpin(void)
{
	g.pinned = false;
}
