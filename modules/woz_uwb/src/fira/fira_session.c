/** @file fira_session.c — Range + URSK store for the CCC Pre-POLL responder. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "fira_session.h"

#if defined(CONFIG_WOZ_ALIRO)
#include "aliro_kdf.h"

/* Aliro URSK stash: the Pre-POLL decode reads it to derive the CCC STS. */
static bool    g_have_ursk;
static uint8_t g_ursk[ALIRO_URSK_LEN];

void fira_session_set_provisioned_ursk(const uint8_t *ursk)
{
	if (ursk == NULL) {
		g_have_ursk = false;
		memset(g_ursk, 0, sizeof(g_ursk));
		return;
	}
	memcpy(g_ursk, ursk, sizeof(g_ursk));
	g_have_ursk = true;
}

const uint8_t *fira_session_get_ursk(void)
{
	return g_have_ursk ? g_ursk : NULL;
}

uint32_t fira_session_current_slot(void)
{
	/* The live index comes from the air, so this diagnostic counter is inert and reads 0. */
	return 0u;
}
#endif /* CONFIG_WOZ_ALIRO */

/* Last valid DS-TWR range, latched for the Aliro mRangingData seam and the shell. */
static bool     g_have_range;
static int32_t  g_last_range_cm;
static uint16_t g_last_range_addr;
static uint8_t  g_last_range_nlos;
static uint32_t g_last_range_block;
static int64_t  g_last_range_ms;

/** @brief Fetch the most recent valid DS-TWR range; out-params optional (NULL to skip). */
bool fira_session_last_range(int32_t *cm_out, uint16_t *addr_out,
			     uint8_t *nlos_out, uint32_t *block_out,
			     int64_t *age_ms_out)
{
	if (!g_have_range) {
		return false;
	}
	if (cm_out) {
		*cm_out = g_last_range_cm;
	}
	if (addr_out) {
		*addr_out = g_last_range_addr;
	}
	if (nlos_out) {
		*nlos_out = g_last_range_nlos;
	}
	if (block_out) {
		*block_out = g_last_range_block;
	}
	if (age_ms_out) {
		*age_ms_out = k_uptime_get() - g_last_range_ms;
	}
	return true;
}

#if defined(CONFIG_WOZ_ALIRO)
void fira_session_set_ccc_range_cm(int32_t cm, uint32_t block)
{
	/* Latch the CCC responder's distance into the store the mRangingData seam reads. */
	g_have_range = true;
	g_last_range_cm = (cm < 0) ? 0 : cm;
	g_last_range_addr = 0u;
	g_last_range_nlos = 0u;
	g_last_range_block = block;
	g_last_range_ms = k_uptime_get();
}
#endif /* CONFIG_WOZ_ALIRO */
