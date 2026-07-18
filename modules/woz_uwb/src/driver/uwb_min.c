/** @file uwb_min.c — DW3110 bring-up driver (implementation). */

#include "uwb_min.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* dwt_uwb_driver public headers; the include is unprefixed. */
#include <deca_device_api.h>
#include <deca_probe_interface.h>
#include <dw3000_hw.h>

LOG_MODULE_REGISTER(uwb_min, LOG_LEVEL_INF);

/* Idempotent-init flags: g_probed covers the chipid path, g_radio_ready the radio path. */
static bool g_probed;
static bool g_radio_ready;

/** @brief Bring the SDK up to "probed" state on first call; no-op afterwards. */
static int uwb_probe_ensure(void)
{
	if (g_probed) {
		return 0;
	}

	dw3000_hw_init();
	dw3000_hw_reset();
	/* Datasheet: ~2 ms wakeup latency after reset; 5 ms gives margin. */
	k_msleep(5);

	// Struct type passed to dwt_probe to initialize the DW3000 device; contains
	// platform-specific probe parameters.
	int err = dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);
	if (err != DWT_SUCCESS) {
		LOG_ERR("dwt_probe failed: %d", err);
		return -EIO;
	}

	g_probed = true;
	return 0;
}

/** @brief Default radio configuration (channel 9, 6.8 Mbps). */
static const dwt_config_t g_uwb_cfg = {
	.chan = 9,
	.txPreambLength = DWT_PLEN_128,
	.rxPAC = DWT_PAC8,
	/* Preamble code 11: a compatible baseline; the responder reconfigures per the ranging
	   config. */
	.txCode = 11,
	.rxCode = 11,
	/* SP3 static STS uses the IEEE 802.15.4z 8-bit binary SFD, not the legacy Decawave pattern.
	 */
	.sfdType = DWT_SFD_IEEE_4Z,
	.dataRate = DWT_BR_6M8,
	.phrMode = DWT_PHRMODE_STD,
	.phrRate = DWT_PHRRATE_STD,
	.sfdTO = (129 + 8 - 8), /* preamble + SFD - PAC, per Qorvo formula */
	/* SP3 framing (STS-no-data) without the SDC bit. */
	.stsMode = DWT_STS_MODE_ND,
	.stsLength = DWT_STS_LEN_64,
	.pdoaMode = DWT_PDOA_M0,
};

/** @brief Default TX power / pulse-shaper config (channel 9). */
static const dwt_txconfig_t g_uwb_txcfg = {
	.PGdly = 0x34,
	.power = 0xfdfdfdfdUL,
	.PGcount = 0,
};

/** @brief Bring the SDK up to "radio configured + LEDs on" state. */
static int uwb_radio_ensure_init(void)
{
	if (g_radio_ready) {
		return 0;
	}

	int err = uwb_probe_ensure();
	if (err) {
		return err;
	}

	if (dwt_initialise(DWT_DW_INIT) != DWT_SUCCESS) {
		LOG_ERR("dwt_initialise failed");
		return -EIO;
	}

	if (dwt_configure((dwt_config_t *)&g_uwb_cfg) != DWT_SUCCESS) {
		LOG_ERR("dwt_configure failed");
		return -EIO;
	}

	dwt_configuretxrf((dwt_txconfig_t *)&g_uwb_txcfg);

	/* Configure sleep/wake: restore config + go to IDLE_PLL on wake, wake on chip-select,
	 * re-run SAR. */
	dwt_configuresleep(DWT_CONFIG | DWT_GOTOIDLE | DWT_RUNSAR, DWT_WAKE_CSN | DWT_SLP_EN);

	/* INIT_BLINK | ENABLE: flash both LEDs once at setup to verify the LED lines. */
	dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

	/* Wire the chip's IRQ line into Zephyr's GPIO framework so RX/TX events reach our
	 * callbacks. */
	(void)dw3000_hw_init_interrupt();

	g_radio_ready = true;
	return 0;
}

int uwb_min_radio_init(void)
{
	return uwb_radio_ensure_init();
}

int uwb_min_hw_reset(void)
{
	/* Drive the SDK's reset routine (routed through the platform glue). */
	dw3000_hw_reset();
	k_msleep(5);

	/* Force a re-probe and re-init after reset (the SDK's fn-pointer table gets cleared). */
	g_probed = false;
	g_radio_ready = false;
	return 0;
}

int uwb_min_read_chipid(uint32_t *id_out)
{
	if (id_out == NULL) {
		return -EINVAL;
	}

	int err = uwb_probe_ensure();
	if (err) {
		return err;
	}

	*id_out = dwt_readdevid();
	return 0;
}

/* Status-bit masks for clearing after each phase; kept minimal to avoid stomping unrelated bits. */
#define TX_STATUS_CLEAR_MASK                                                                       \
	(DWT_INT_TXFRS_BIT_MASK | DWT_INT_TXPHS_BIT_MASK | DWT_INT_TXPRS_BIT_MASK |                \
	 DWT_INT_TXFRB_BIT_MASK)

#define RX_STATUS_EVENT_MASK                                                                       \
	(DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFTO_BIT_MASK |                \
	 DWT_INT_RXPTO_BIT_MASK | DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXSTO_BIT_MASK |                \
	 DWT_INT_RXFSL_BIT_MASK | DWT_INT_ARFE_BIT_MASK)

int uwb_min_selftest(struct uwb_selftest_result *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));

	int err = uwb_radio_ensure_init();
	if (err) {
		return err;
	}

	/* ── TX phase ─────────────────────────────────────────────── */

	/* Short ASCII payload; the SDK appends the 16-bit FCS, so the length is data_len + 2. */
	static const uint8_t payload[] = {'Z', 'I', 'O', 'N', 'U', 'W', 'B'};
	const uint16_t frame_len = sizeof(payload) + 2; /* +FCS */

	if (dwt_writetxdata(sizeof(payload), (uint8_t *)payload, 0) != DWT_SUCCESS) {
		LOG_ERR("dwt_writetxdata failed");
		return -EIO;
	}
	/* ranging=1 tags the frame as a ranging frame; harmless for the self-test. */
	dwt_writetxfctrl(frame_len, 0, 1);

	if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
		LOG_ERR("dwt_starttx failed");
		return -EIO;
	}

	/* Poll SYS_STATUS for TXFRS; 100 ms ceiling is generous. */
	uint32_t tx_status = 0;
	const int64_t tx_deadline = k_uptime_get() + 100;
	while (k_uptime_get() < tx_deadline) {
		tx_status = dwt_readsysstatuslo();
		if (tx_status & DWT_INT_TXFRS_BIT_MASK) {
			out->tx_done = true;
			break;
		}
	}
	out->tx_status = tx_status;

	if (!out->tx_done) {
		LOG_ERR("TX timed out: SYS_STATUS=0x%08x", tx_status);
		/* Tear-down: force radio off so the RX phase doesn't race a hung TX. */
		dwt_forcetrxoff();
		return -EIO;
	}

	/* Write-1-to-clear TX status bits so the RX phase sees a clean SYS_STATUS. */
	dwt_writesysstatuslo(TX_STATUS_CLEAR_MASK);

	/* ── RX phase ─────────────────────────────────────────────── */

	/* RX timeout register units are ~1.0256 us; ~97500 is about 100 ms. */
	dwt_setrxtimeout(97500);

	if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
		LOG_ERR("dwt_rxenable failed");
		return -EIO;
	}
	out->rx_armed = true;

	/* Poll for any RX event; with no peer we expect RXFTO after the window. */
	uint32_t rx_status = 0;
	const int64_t rx_deadline = k_uptime_get() + 200; /* 2× chip TO */
	while (k_uptime_get() < rx_deadline) {
		rx_status = dwt_readsysstatuslo();
		if (rx_status & RX_STATUS_EVENT_MASK) {
			out->rx_event = true;
			break;
		}
	}
	out->rx_status = rx_status;

	/* Force the radio off at end-of-test so it isn't left in RX listen. */
	dwt_forcetrxoff();
	/* Clear the RX-event bits we just observed. */
	dwt_writesysstatuslo(rx_status & RX_STATUS_EVENT_MASK);

	return 0;
}

/** @brief Shared static STS test vector for the two-device raw loopback. */
static const uint8_t g_partner_sts_key[16] = {
	0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xF0, 0x0D,
	0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
};
static const uint8_t g_partner_sts_iv[16] = {
	0x01, 0x00, 0x00, 0x00, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
};

int uwb_min_twr_prep(void)
{
	int err = uwb_radio_ensure_init();
	if (err) {
		return err;
	}

	/* Re-apply the baseline PHY; STS is the caller's to program, so none is set here. */
	dwt_forcetrxoff();
	if (dwt_configure((dwt_config_t *)&g_uwb_cfg) != DWT_SUCCESS) {
		LOG_ERR("rawtwr: dwt_configure failed");
		return -EIO;
	}
	dwt_configuretxrf((dwt_txconfig_t *)&g_uwb_txcfg);

	/* RX window for the RESP; ~50 ms is a generous ceiling that only bounds the timeout case.
	 */
	dwt_setrxtimeout(50000);
	return 0;
}

void uwb_min_twr_exchange(struct uwb_twr_frame *f)
{
	static const uint8_t poll_msg[] = {'P', 'A', 'R', 'T', 'P', 'O', 'L', 'L'};
	const uint32_t rx_mask =
		RX_STATUS_EVENT_MASK | DWT_INT_CIAERR_BIT_MASK | DWT_INT_CPERR_BIT_MASK;
	const uint32_t to_mask =
		DWT_INT_RXFTO_BIT_MASK | DWT_INT_RXPTO_BIT_MASK | DWT_INT_RXSTO_BIT_MASK;

	memset(f, 0, sizeof(*f));
	dwt_forcetrxoff();

	/* STS key+IV + counter reset for TX are the caller's responsibility. */
	if (dwt_writetxdata(sizeof(poll_msg), (uint8_t *)poll_msg, 0) != DWT_SUCCESS) {
		return;
	}
	dwt_writetxfctrl(sizeof(poll_msg) + 2u, 0, 1); /* +FCS, ranging=1 */
	if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
		return;
	}

	/* Wait for the POLL to leave the antenna. */
	int64_t txdl = k_uptime_get() + 50;
	while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) {
		if (k_uptime_get() > txdl) {
			break;
		}
	}
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	f->tx_ok = true;

	/* Arm RX for the RESP; reset the STS counter to the loaded IV so it correlates. */
	dwt_configurestsloadiv();
	dwt_rxenable(DWT_START_RX_IMMEDIATE);

	uint32_t st = 0;
	int64_t rxdl = k_uptime_get() + 80; /* > the 50 ms chip RX timeout */
	while (!((st = dwt_readsysstatuslo()) & rx_mask)) {
		if (k_uptime_get() > rxdl) {
			break;
		}
	}

	const bool got_frame = (st & (DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXFR_BIT_MASK)) != 0;
	f->timed_out = (st & to_mask) != 0 && !got_frame;
	if (!f->timed_out) {
		(void)dwt_readstsquality(&f->sts, 0);
	}
	f->status = st;

	dwt_forcetrxoff();
	dwt_writesysstatuslo(st & rx_mask);
}

int uwb_min_twr_poll(uint32_t n, uint32_t period_ms, struct uwb_twr_result *out)
{
	if (out == NULL) {
		return -EINVAL;
	}
	memset(out, 0, sizeof(*out));

	int err = uwb_min_twr_prep();
	if (err) {
		return err;
	}

	/* Program the fixed static STS key + IV directly (the CCC wrap passes through while
	 * unbound). */
	dwt_configurestskey((dwt_sts_cp_key_t *)g_partner_sts_key);
	dwt_configurestsiv((dwt_sts_cp_iv_t *)g_partner_sts_iv);
	dwt_configurestsloadiv();

	for (uint32_t i = 0; i < n; i++) {
		struct uwb_twr_frame f;

		dwt_configurestsloadiv(); /* static STS: reset the counter for TX. */
		uwb_min_twr_exchange(&f);

		if (f.tx_ok) {
			out->polls_tx++;
		}
		if (!f.timed_out) {
			out->resp_rx++;
			if (f.sts > 0) {
				out->sts_ok++;
			}
		}
		out->last_sts = f.sts;
		out->last_status = f.status;
		LOG_INF("rawtwr poll=%u %s sts=%d status=%08X", i, f.timed_out ? "TO" : "RESP",
			f.sts, (unsigned int)f.status);

		if (period_ms) {
			k_msleep(period_ms);
		}
	}
	return 0;
}
