/** @file fira_session.c — Range + URSK store for the CCC Pre-POLL responder. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "woz_port.h"

#include "fira_session.h"

#if defined(CONFIG_WOZ_ALIRO)
#include "aliro_kdf.h"

/* Aliro URSK stash: the Pre-POLL decode reads it to derive the CCC STS. */
static bool g_have_ursk;
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
static bool g_have_range;
static int32_t g_last_range_cm;
static uint16_t g_last_range_addr;
static uint8_t g_last_range_nlos;
static uint32_t g_last_range_block;
static int64_t g_last_range_ms;
/* Layer 4: run length of consecutive plausible, mutually consistent blocks. */
static uint8_t g_range_trust;
#if defined(CONFIG_WOZ_ALIRO)
/* Fired (from the UWB RX path) after each accepted latch, so the unlock seam
 * can wake on fresh ranges instead of polling. Keep the callback tiny. */
static void (*g_range_listener)(void);
#endif

/** @brief Fetch the most recent valid DS-TWR range; out-params optional (NULL to skip). */
bool fira_session_last_range(int32_t *cm_out, uint16_t *addr_out, uint8_t *nlos_out,
			     uint32_t *block_out, int64_t *age_ms_out)
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
		*age_ms_out = woz_uptime_ms() - g_last_range_ms;
	}
	return true;
}

#if defined(CONFIG_WOZ_ALIRO)
bool fira_session_range_plausible(int32_t cm)
{
	return cm >= -FIRA_RANGE_NEG_TOL_CM && cm <= FIRA_RANGE_MAX_CM;
}

bool fira_session_sts_quality_ok(int32_t driver_verdict, int16_t quality_index)
{
	return driver_verdict >= 0 && quality_index >= FIRA_STS_QUALITY_MIN;
}

bool fira_session_range_trusted(void)
{
	return g_range_trust >= FIRA_RANGE_TRUST_K;
}

void fira_session_set_range_listener(void (*cb)(void))
{
	g_range_listener = cb;
}

void fira_session_set_ccc_range_cm(int32_t cm, uint32_t block)
{
	int32_t delta;

	/* Layer 1: reject a physically impossible / out-of-envelope measurement
	 * outright — never let a spoofed negative masquerade as 0 cm. The prior
	 * good range and its trust stay put; an implausible block earns none. */
	if (!fira_session_range_plausible(cm)) {
		g_range_trust = 0u;
		return;
	}
	/* Legitimate point-blank reads dip slightly negative (calibration slop). */
	if (cm < 0) {
		cm = 0;
	}

	/* Layer 4: consecutive plausible blocks that agree build trust; a jump
	 * restarts the run. The block still latches, so telemetry stays live. */
	delta = (cm >= g_last_range_cm) ? (cm - g_last_range_cm) : (g_last_range_cm - cm);
	if (g_have_range && delta <= FIRA_RANGE_SPREAD_CM) {
		if (g_range_trust < FIRA_RANGE_TRUST_K) {
			g_range_trust++;
		}
	} else {
		g_range_trust = 1u; /* first agreeing sample of a (new) run */
	}

	/* Latch the CCC responder's distance into the store the mRangingData seam reads. */
	g_have_range = true;
	g_last_range_cm = cm;
	g_last_range_addr = 0u;
	g_last_range_nlos = 0u;
	g_last_range_block = block;
	g_last_range_ms = woz_uptime_ms();

	/* Accepted latches only: a rejected block must never wake the unlock seam. */
	if (g_range_listener != NULL) {
		g_range_listener();
	}
}
#endif /* CONFIG_WOZ_ALIRO */
