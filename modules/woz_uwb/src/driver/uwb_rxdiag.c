/** @file uwb_rxdiag.c — Diagnostic RX/TX event tallies + ranging heartbeat. */

#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <deca_device_api.h>

#include "ccc_shim.h"     /* ccc_shim_rx_notify_rx — empirical STS-index tracker */
#include "fira_session.h" /* fira_session_last_range — latched DS-TWR distance */
#include "uwb_rxdiag.h"   /* our accessors + runtime stream toggle */
#include "woz_diag.h"     /* DIAGK — per-event/cfg/CAD trace, gated off in pretty mode */
#include "woz_alloc.h"    /* qrtc_get_us — monotonic microsecond wall-clock */

LOG_MODULE_REGISTER(uwb_rxdiag, LOG_LEVEL_INF);

/* ANSI for the colored heartbeat line (printk, so no `<inf>` prefix to fight). */
#define RX_RST "\x1b[0m"
#define RX_DIM "\x1b[2m"
#define RX_GRN "\x1b[32m"
#define RX_YEL "\x1b[33m"
#define RX_RED "\x1b[31m"
#define RX_CYN "\x1b[36m"

/** @brief Runtime arm state for the periodic heartbeat (backs `aliro log`). */
static volatile bool g_stream;

/** @brief Runtime arm state for the per-block distance stream (backs `aliro frames`). */
static volatile bool g_rng_stream;

/** @brief The real registration, reachable past the ld --wrap. */
void __real_dwt_setcallbacks(dwt_callbacks_s *callbacks);

/** @brief The blob's own callbacks, saved so our shims can chain to them. */
static dwt_cb_t g_blob_rxok, g_blob_rxto, g_blob_rxerr, g_blob_txdone;

/** @brief Running RX/TX event tallies + the last status word per class. */
static volatile uint32_t g_rxok, g_rxto, g_rxerr, g_txdone;
static volatile uint32_t g_last_err_status, g_last_ok_status;

/** @brief Per-event frame-structure log (first @ref RXDIAG_EVENT_LOG RX events). */
#define RXDIAG_EVENT_LOG                                                                           \
	4 /* trimmed from 24: per-event printk starves the RX re-arm (Pre-POLL hunt) */
static uint32_t g_ev_logged;

/** @brief RX-arrival cadence histogram: bins each RX phase within the 192 ms block grid. */
#define CAD_BLOCK_US 192000u
#define CAD_BIN_US   2000u
#define CAD_BINS     (CAD_BLOCK_US / CAD_BIN_US) /* 96 */
static uint32_t g_cad[CAD_BINS];
static uint32_t g_cad_n;

/** @brief Bin one RX detection's phase within the 192 ms block grid. */
static void cad_mark(void)
{
	uint32_t phase = (uint32_t)((uint64_t)qrtc_get_us() % CAD_BLOCK_US);
	g_cad[phase / CAD_BIN_US]++;
	g_cad_n++;
}

/** @brief SYS_CFG + STS-packet-config (CP_SPC): the armed SP mode (0=SP0..3=SP3). */
#define RXDIAG_SYS_CFG   0x10UL
#define RXDIAG_CP_SPC(v) (unsigned)(((v) >> 12) & 0x3u)

/** @brief Log one RX event's frame structure until the budget is spent. */
static void rxdiag_ev_log(const char *cls, const dwt_cb_data_t *d)
{
	if (d != NULL && g_ev_logged < RXDIAG_EVENT_LOG) {
		/* Read before chaining so sp is the mode of THIS reception, not the next window. */
		unsigned sp = RXDIAG_CP_SPC(dwt_read_reg(RXDIAG_SYS_CFG));

		DIAGK("rxdiag ev#%u %s sp%u st=%08x len=%u fl=%02x\n", (unsigned)g_ev_logged, cls,
		      sp, (unsigned)d->status, (unsigned)d->datalength, (unsigned)d->rx_flags);
		g_ev_logged++;
	}
}

/** @brief RX-good shim: tally + latch status, feed the index tracker, then chain. */
/** @brief Decode-cost probe: last try_prepoll() duration, hi32 (~4 ns) units. */
uint32_t g_ccc_dbg_decode;

// RX-good callback shim: log RX diagnostics, invoke the armed CCC callback if set (SP3 POLL arm),
// then decode the Pre-POLL frame off the critical path to warm the next block's STS.
static void shim_rxok(const dwt_cb_data_t *d)
{
	bool await = ccc_shim_rx_awaiting_poll();

	g_rxok++;
	cad_mark();
	if (d != NULL) {
		g_last_ok_status = d->status;
		/* The empirical STS-index tracker owns per-POLL logging + the sweep. */
		ccc_shim_rx_notify_rx(d->status);
	}
	/* sp reflects THIS reception's mode, so log before the arm below re-arms. */
	rxdiag_ev_log("ok", d);
	/* Arm the SP3 POLL window first, ahead of the Pre-POLL decode, using the pre-warmed STS. */
	if (g_blob_rxok != NULL) {
		g_blob_rxok(d);
	}
	/* Decode the Pre-POLL after the arm to warm the next block's index; skip on the POLL event.
	 */
	if (d != NULL && !await) {
		uint32_t s0 = dwt_readsystimestamphi32();

		ccc_shim_rx_try_prepoll(d->datalength);
		g_ccc_dbg_decode = dwt_readsystimestamphi32() - s0;
	}
}

/** @brief RX-timeout shim: tally, then run the blob's handler. */
static void shim_rxto(const dwt_cb_data_t *d)
{
	g_rxto++;
	rxdiag_ev_log("to", d);
	if (g_blob_rxto != NULL) {
		g_blob_rxto(d);
	}
}

/** @brief RX-error shim: tally + latch status (STS/CIA bits), then chain. */
static void shim_rxerr(const dwt_cb_data_t *d)
{
	g_rxerr++;
	cad_mark();
	if (d != NULL) {
		g_last_err_status = d->status;
	}
	rxdiag_ev_log("er", d);
	if (g_blob_rxerr != NULL) {
		g_blob_rxerr(d);
	}
}

/** @brief TX-done shim: tally, then run the blob's handler. */
static void shim_txdone(const dwt_cb_data_t *d)
{
	g_txdone++;
	if (g_blob_txdone != NULL) {
		g_blob_txdone(d);
	}
}

/** @brief Intercept the callback registration and insert counting shims. */
void __wrap_dwt_setcallbacks(dwt_callbacks_s *callbacks)
{
	if (callbacks != NULL) {
		g_blob_rxok = callbacks->cbRxOk;
		g_blob_rxto = callbacks->cbRxTo;
		g_blob_rxerr = callbacks->cbRxErr;
		g_blob_txdone = callbacks->cbTxDone;
		callbacks->cbRxOk = (g_blob_rxok != NULL) ? shim_rxok : NULL;
		callbacks->cbRxTo = (g_blob_rxto != NULL) ? shim_rxto : NULL;
		callbacks->cbRxErr = (g_blob_rxerr != NULL) ? shim_rxerr : NULL;
		callbacks->cbTxDone = (g_blob_txdone != NULL) ? shim_txdone : NULL;
	}
	__real_dwt_setcallbacks(callbacks);
}

/** @brief The real PHY-config entries, reachable past the ld `--wrap`. */
int32_t __real_dwt_configure(dwt_config_t *config);
void __real_dwt_configurestsmode(uint8_t stsMode);

/** @brief Budget for full-PHY-config logs (rare: session setup + reconfigs). */
#define RXDIAG_CFG_LOG 8
static uint32_t g_cfg_logged;

/** @brief Budget for STS-mode-write logs (the only other CP_SPC writer). */
#define RXDIAG_STSMODE_LOG 32
static uint32_t g_stsmode_logged;

/** @brief Log every full PHY configuration the blob issues. */
int32_t __wrap_dwt_configure(dwt_config_t *config)
{
	if (config != NULL && g_cfg_logged < RXDIAG_CFG_LOG) {
		DIAGK("rxdiag cfg#%u chan=%u plen=%u txc=%u rxc=%u sfd=%u rate=%u phrM=%u phrR=%u "
		      "sfdTO=%u sts=%02x stsLen=%u\n",
		      (unsigned)g_cfg_logged, (unsigned)config->chan,
		      (unsigned)config->txPreambLength, (unsigned)config->txCode,
		      (unsigned)config->rxCode, (unsigned)config->sfdType,
		      (unsigned)config->dataRate, (unsigned)config->phrMode,
		      (unsigned)config->phrRate, (unsigned)config->sfdTO, (unsigned)config->stsMode,
		      (unsigned)config->stsLength);
		g_cfg_logged++;
	}
	return __real_dwt_configure(config);
}

/** @brief Log every STS-mode (CP_SPC) write the blob issues, then pass through. */
void __wrap_dwt_configurestsmode(uint8_t stsMode)
{
	if (g_stsmode_logged < RXDIAG_STSMODE_LOG) {
		DIAGK("rxdiag stsmode#%u=%02x\n", (unsigned)g_stsmode_logged, (unsigned)stsMode);
		g_stsmode_logged++;
	}
	__real_dwt_configurestsmode(stsMode);
}

static void rxdiag_log(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(g_rxdiag_work, rxdiag_log);

/* A DS-TWR range older than this is shown dimmed as stale, not live cyan. */
#define RXDIAG_STALE_MS 1000

/** @brief Periodic ranging heartbeat (every 2 s); re-arms itself while streaming. */
static void rxdiag_log(struct k_work *work)
{
	ARG_UNUSED(work);

	/* "Idle" == no new GOOD frame since the last heartbeat; announce idle once, then stay
	 * quiet. */
	static uint32_t last_rxok;
	static bool idle_announced;
	bool active = (g_rxok != last_rxok);

	last_rxok = g_rxok;

	if (!active) {
		if (!idle_announced) {
			printk(RX_DIM "  ⟐ idle · no ranging · sts%s" RX_RST "\n",
			       ccc_shim_active() ? RX_GRN "●" RX_DIM : "○");
			idle_announced = true;
		}
	} else {
		idle_announced = false;

		int32_t cm = 0;
		int64_t age_ms = 0;
		bool have_range = fira_session_last_range(&cm, NULL, NULL, NULL, &age_ms);
		bool sts_live = ccc_shim_active();

		printk(RX_DIM "  ⟐ " RX_RST RX_CYN "rx" RX_RST " " RX_GRN "✓%u" RX_RST
			      " %s✗%u" RX_RST " " RX_YEL "⧗%u" RX_RST RX_DIM " tx%u" RX_RST,
		       g_rxok, g_rxerr ? RX_RED : RX_DIM, g_rxerr, g_rxto, g_txdone);
		if (have_range) {
			/* Fresh range is live cyan; a stale one is dimmed with its age. */
			if (age_ms > RXDIAG_STALE_MS) {
				printk(RX_DIM " · %dcm stale %llds" RX_RST, cm,
				       (long long)(age_ms / 1000));
			} else {
				printk(RX_DIM " · " RX_RST RX_CYN "%dcm" RX_RST, cm);
			}
		}
		printk(RX_DIM " · " RX_RST "sts%s" RX_RST "\n", sts_live ? RX_GRN "●" : RX_DIM "○");
		/* Cadence: peak bin vs mean reveals a 192 ms-grid cluster (phone) vs uniform
		 * (noise). */
		if (g_cad_n > 0u) {
			uint32_t pk = 0u, pki = 0u, pk2 = 0u, pk2i = 0u;
			for (uint32_t i = 0u; i < CAD_BINS; i++) {
				if (g_cad[i] > pk) {
					pk2 = pk;
					pk2i = pki;
					pk = g_cad[i];
					pki = i;
				} else if (g_cad[i] > pk2) {
					pk2 = g_cad[i];
					pk2i = i;
				}
			}
			uint32_t ratio_x10 = (pk * CAD_BINS * 10u) / g_cad_n; /* (peak/mean) x10 */
			DIAGK("CAD n=%u peak=b%u:%u(%ums) 2nd=b%u:%u(%ums) mean=%u pk/mean=%u.%u\n",
			      g_cad_n, pki, pk, pki * 2u, pk2i, pk2, pk2i * 2u, g_cad_n / CAD_BINS,
			      ratio_x10 / 10u, ratio_x10 % 10u);
		}
	}
	if (g_stream) {
		k_work_reschedule(&g_rxdiag_work, K_SECONDS(2));
	}
}

void uwb_rxdiag_get_counts(uint32_t *rxok, uint32_t *rxerr, uint32_t *rxto, uint32_t *txdone,
			   uint32_t *last_err, uint32_t *last_ok)
{
	if (rxok) {
		*rxok = g_rxok;
	}
	if (rxerr) {
		*rxerr = g_rxerr;
	}
	if (rxto) {
		*rxto = g_rxto;
	}
	if (txdone) {
		*txdone = g_txdone;
	}
	if (last_err) {
		*last_err = g_last_err_status;
	}
	if (last_ok) {
		*last_ok = g_last_ok_status;
	}
}

void uwb_rxdiag_stream_set(bool on)
{
	g_stream = on;
	if (on) {
		k_work_reschedule(&g_rxdiag_work, K_NO_WAIT);
	} else {
		k_work_cancel_delayable(&g_rxdiag_work);
	}
}

bool uwb_rxdiag_stream_get(void)
{
	return g_stream;
}

void uwb_rxdiag_rng_set(bool on)
{
	g_rng_stream = on;
}

bool uwb_rxdiag_rng_get(void)
{
	return g_rng_stream;
}

/** @brief Arm the periodic heartbeat at application init. */
static int rxdiag_init(void)
{
	g_stream = !IS_ENABLED(CONFIG_WOZ_PRETTY_SHELL);
	if (g_stream) {
		k_work_reschedule(&g_rxdiag_work, K_SECONDS(2));
	}
	return 0;
}

SYS_INIT(rxdiag_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
