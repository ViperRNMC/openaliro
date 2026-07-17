// UWB facade: binds the CCC credential-based STS engine to the DW3000 radio, exposes Aliro DS-TWR responder start/stop and range query, and manages platform dependencies (HFCLK boost, SPI init, callbacks).
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 * C shim over fira_session + ccc_shim (see woz_uwb_facade.h).
 */
#include "woz_uwb_facade.h"
#include <errno.h>
#include "aliro_kdf.h"
#include "fira_session.h"
#include "ccc_shim.h"

#if defined(CONFIG_SOC_NRF5340_CPUAPP)
#include <hal/nrf_clock.h>
#endif

/**
 * @brief One-shot boost of the app-core HFCLK to 128 MHz for the DW3000 SPI bus.
 *
 * nRF5340-specific platform seam: the app core boots with HFCLK divided to
 * 64 MHz. Other SoCs (e.g. ESP32-S3) clock their SPI controller independently,
 * so this compiles to a no-op there. See docs/porting.md.
 */
static void woz_hfclk_ensure_128mhz(void)
{
#if defined(CONFIG_SOC_NRF5340_CPUAPP)
	static bool boosted;

	if (!boosted) {
		nrf_clock_hfclk_div_set(NRF_CLOCK, NRF_CLOCK_HFCLK_DIV_1);
		boosted = true;
	}
#endif
}

int woz_uwb_bind_ursk(const uint8_t *ursk, size_t ursk_len)
{
	woz_hfclk_ensure_128mhz();
	fira_session_set_provisioned_ursk(ursk);
	/* Placeholder ranging_config / sts_index0 / n_slot until Aliro M1-M4 negotiation. */
	return ccc_shim_bind_from_ursk(ursk, ursk, ursk_len, 0u, 8u);
}

int woz_uwb_start_aliro(const struct woz_uwb_aliro_cfg *c)
{
	if (c == NULL || c->ursk == NULL) {
		return -EINVAL;
	}

	woz_hfclk_ensure_128mhz();

	/* Stash the URSK so the Pre-POLL decode can derive the CCC STS the Wallet expects. */
	fira_session_set_provisioned_ursk(c->ursk);
	/* Bind the shim's SaltedHash to the RangingConfiguration when supplied; else fall back to the URSK. */
	if (c->ranging_config != NULL && c->rc_len > 0u) {
		ccc_shim_bind_from_ursk(c->ursk, c->ranging_config, c->rc_len,
					c->sts_index0,
					c->slot_per_round ? c->slot_per_round : 1u);
	} else {
		ccc_shim_bind_from_ursk(c->ursk, c->ursk, ALIRO_URSK_LEN,
					c->sts_index0,
					c->slot_per_round ? c->slot_per_round : 1u);
	}
	/* Fresh per-session log budget so a live Wallet session re-logs its own RX-arms. */
	ccc_shim_rx_log_reset();

	/* Stand up the permanent SP0 receiver on deps/dw3000, driving the DS-TWR exchange. */
	return ccc_prepoll_listen(c->channel, c->sync_code_index);
}

void woz_uwb_stop(void)
{
	/* Unbind the CCC STS so the permanent Pre-POLL receiver ignores frames. */
	ccc_shim_unbind();
	/* Close the listen-gate and force the DW3000 out of RX so the SP0
	 * Pre-POLL listener stops self-rearming (RX LED dark until restart). */
	ccc_prepoll_stop();
}

bool woz_uwb_last_range_cm(int32_t *cm_out)
{
	return fira_session_last_range(cm_out, NULL, NULL, NULL, NULL);
}

bool woz_uwb_trusted_range_cm(int32_t *cm_out)
{
#if defined(CONFIG_WOZ_ALIRO)
	/* Layer 4: only surface the range to the unlock seam once K consecutive
	 * agreeing plausible blocks have built trust, so a lone spoofed block
	 * cannot flip open-allowed. */
	return fira_session_last_range(cm_out, NULL, NULL, NULL, NULL) &&
	       fira_session_range_trusted();
#else
	return fira_session_last_range(cm_out, NULL, NULL, NULL, NULL);
#endif
}
