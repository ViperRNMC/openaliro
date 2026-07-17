/** @file ccc_shim_rx.c — responder-RX CCC STS substitution (ld --wrap=dwt_rxenable) programming the CCC STS on each RX-arm; target only. */

#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <deca_device_api.h>

#include "ccc_shim.h"
#include "ccc_kdf.h"      /* CCC_DURSK_LEN / CCC_STS_V_LEN + mUPSK/UAD/SP0 crypto */
#include "ccc_mac.h"      /* ccc_parse_mhr / ccc_pre_poll_parse — Pre-POLL decode */
#include "fira_session.h" /* fira_session_current_slot / fira_session_get_ursk */
#include "uwb_min.h"      /* uwb_min_radio_init — standalone SP0 Pre-POLL listener */
#include "woz_diag.h"     /* DIAGK — verbose per-frame trace, gated off in pretty mode */
#include "uwb_rxdiag.h"   /* uwb_rxdiag_stream_get — the `aliro log` runtime toggle */

#if defined(CONFIG_WOZ_PRETTY_SHELL)
#include <zephyr/logging/log.h>
/* Pretty mode: one curated line per ranging block replaces the per-frame trace. */
LOG_MODULE_REGISTER(woz_rng, LOG_LEVEL_INF);
#endif

/** @brief Log the first N intercepted RX-arms — confirms slot advance + register load. */
#define CCC_RX_LOG_ARMS 16

/** @brief STS_KEY/STS_IV registers (dw3000_deca_regs.h) — read back to prove the load lands. */
#define STS_KEY0_REG 0x2000CUL
#define STS_KEY1_REG 0x20010UL
#define STS_KEY2_REG 0x20014UL
#define STS_KEY3_REG 0x20018UL
#define STS_IV0_REG  0x2001CUL
#define STS_IV1_REG  0x20020UL
#define STS_IV2_REG  0x20024UL
#define STS_IV3_REG  0x20028UL
/** @brief CHAN_CTRL (dw3000_deca_regs.h): RX preamble code [12:8], SFD type [2:1]; read at the POLL arm to prove the active PHY. */
#define CHAN_CTRL_REG 0x10014UL
/** @brief SYS_CFG [13:12] = CP_SPC (STS packet config / SP mode: 3 = SP3/ND). */
#define SYS_CFG_REG 0x10UL
/** @brief STS_CFG0 [7:0] = CPS_LEN (STS length code: 7 = STS64 = 4096 DRBG bits). */
#define STS_CFG0_REG 0x20000UL

/** ISOLATION PROBE (bench) — pin the RX STS index to the deterministic round-0 POLL (STS_Index0 + 1) instead of the local-time slot. */
#define CCC_RX_PIN_ROUND0_POLL 1

/** ISOLATION PROBE (bench) — force SP3/ND at each RX arm so the STS engine is engaged when Apple's SP3 POLL lands. */
#define CCC_RX_FORCE_SP3 1 /* 1 for the seeded Δ sweep: engage the STS engine on the SP3 POLL. */

/** EMPIRICAL STS-INDEX LOCK (bench) — track Apple's ranging clock on-air by searching the constant slot offset Δ between our time0 and Apple's UWB_Time0. */
#define CCC_RX_LOCK_SWEEP 0 /* 0 for the Pre-POLL listen (WOZ_CCC_PREPOLL_LISTEN): SP0 frames have no
			     * STS, so cper=0 would false-LOCK the sweep tracker — keep notify_rx a no-op. */

/** RESIDUE-COMPLETE alignment-offset sweep (slot = current_slot() + Δ): the STS index is linear at one per slot, so sweep the constant offset Δ. */
#define CCC_RX_SLOTS_PER_BLOCK 96u /**< 192 ms / 2 ms — phase-log modulus only, NOT the index stride. */
/** Sweep CENTER = Apple's UWB_Time0 / slot-duration; the offset is the constant slot gap between our time0 and Apple's UWB_Time0, searched not derived. */
#define CCC_RX_DELTA_SEED 1299 /**< center: 0x0027a4d4 µs / 2 ms (heuristic; retunable). */
#define CCC_RX_DELTA_HALF 768u /**< ± window around the seed: covers offsets [531,2067] (two prior estimates ≈1250–1300). */
#define CCC_RX_N_CAND (2u * CCC_RX_DELTA_HALF) /**< 1536 one-slot Δ candidates, alternating outward from the seed. */
#define CCC_RX_DWELL  1u  /**< catches held per Δ candidate (1 = fast full-range sweep; bump if it cycles past). */

/** @brief The real decadriver entries, reachable past the ld `--wrap`. */
int32_t __real_dwt_rxenable(int32_t mode);
void __real_dwt_configurestsiv(dwt_sts_cp_iv_t *pStsIv);
/* Unconditional: both the FORCE_SP3 probe and the Pre-POLL POLL follow-on flip STS mode; the symbol exists via the rxdiag --wrap=dwt_configurestsmode. */
void __real_dwt_configurestsmode(uint8_t stsMode);

/** @brief Count of intercepted RX-arms; the first @ref CCC_RX_LOG_ARMS are logged. */
static uint32_t g_rx_arms;

/* --- SP3 POLL follow-on (WOZ_CCC_PREPOLL_LISTEN): after a Pre-POLL, arm the STS receiver for the POLL one slot later at Apple's decrypted Poll_STS_Index. --- */
/** @brief Apple's Poll_STS_Index from the most recent decoded Pre-POLL. */
static uint32_t g_poll_sts_index;
/** @brief True once a Pre-POLL has handed us a fresh @ref g_poll_sts_index. */
static bool g_have_poll_index;
/** @brief True while the SP3 RX is armed for the POLL (next RX event = its result). */
static bool g_await_poll;
/** @brief Poll_STS_Index already armed once — a re-detected Pre-POLL (same index) must not re-arm late in the block. */
static uint32_t g_armed_index;
/** @brief Pre-POLL Ipatov timestamp of the in-flight POLL arm (for the gap `d=` log). */
static uint32_t g_prepoll_ip;
/** @brief Poll_STS_Index delta between consecutive Pre-POLLs (the block stride, ~96); primes the next block's dURSK. */
static uint32_t g_poll_stride;
/** @brief STS pre-derived in the idle for the predicted next POLL index, so the SP3 arm packs it directly with no KDF on the 2 ms path. */
static bool     g_warm_valid;
static uint32_t g_warm_index;
static uint8_t  g_warm_dursk[CCC_DURSK_LEN];
static uint8_t  g_warm_sts_v[CCC_STS_V_LEN];

/** @brief Response_0 STS (Poll_STS_Index+1): same-round dURSK plus index+1 STS-V, pre-derived in the idle so the TX path runs no KDF. */
static uint8_t  g_warm_resp_dursk[CCC_DURSK_LEN];
static uint8_t  g_warm_resp_sts_v[CCC_STS_V_LEN];
static uint8_t  g_armed_resp_dursk[CCC_DURSK_LEN];
static uint8_t  g_armed_resp_sts_v[CCC_STS_V_LEN];

/** @brief Final STS (Poll_STS_Index+2): the phone's Final RFRAME one slot after our Response, pre-derived and snapshotted at the POLL arm so no KDF runs. */
static uint8_t  g_warm_final_dursk[CCC_DURSK_LEN];
static uint8_t  g_warm_final_sts_v[CCC_STS_V_LEN];
static uint8_t  g_armed_final_dursk[CCC_DURSK_LEN];
static uint8_t  g_armed_final_sts_v[CCC_STS_V_LEN];
/** @brief True while the SP3 RX is armed for the Final (next RX event = its result). */
static bool     g_await_final;
/** @brief POLL RMARKER of the in-flight round, so the TXDONE can anchor the Final window. */
static uint32_t g_poll_ip_for_final;

/** @brief The three responder DS-TWR timestamps, full 40-bit DTU: POLL RX (t2), Response TX (t3), Final RX (t6). */
static uint64_t g_t_poll_rx;
static uint64_t g_t_resp_tx;
static uint64_t g_t_final_rx;

/* Range-integrity gate (layer 2): the Final RFRAME's STS verdict, captured at
 * the Final RX event and consumed once when the matching Final_Data decodes. The
 * verdict defaults fail-closed so a block with no fresh capture cannot pass a
 * strict gate. See fira_session.h for the predicates and thresholds. */
static int32_t  g_final_sts_verdict = -1;
static int16_t  g_final_sts_index;

/** @brief Per-session Pre-POLL decrypt constants (mUPSK1 + UAD-derived src/dest/keysource): depend only on URSK + STS_Index0, derived once and reused. */
static bool     g_uad_cached;
static uint8_t  g_c_mupsk1[CCC_MUPSK1_LEN];
static uint8_t  g_c_src_long[CCC_SRC_LONG_ADDR_LEN];
static uint8_t  g_c_keysource[CCC_KEYSOURCE_LEN];
static uint8_t  g_c_dest[CCC_DEST_SHORT_ADDR_LEN];

/** @brief Pre-POLL frame stashed at RX for a DEFERRED decode: the ~2 ms decrypt+derive must not run between the Pre-POLL and the POLL. */
static uint8_t  g_pp_stash[64];
static uint16_t g_pp_stash_len;
static bool     g_pp_pending;

#if CCC_RX_LOCK_SWEEP
/** @brief Lock/search state for the phase-locked (o,K) tracker. */
static bool	g_locked;	 /**< True once a candidate cleared CPER. */
static uint32_t g_lock_cand;	 /**< The latched candidate index. */
static uint32_t g_sweep_cand;	 /**< Current candidate index in [0, N_CAND). */
static uint32_t g_dwell_cnt;	 /**< Catches held at the current candidate. */
static int32_t  g_cur_delta;	 /**< Signed offset Δ loaded for the in-flight arm (log). */
static uint32_t g_dbg_slot;	 /**< Absolute slot loaded for the in-flight arm (log). */
static uint32_t g_dbg_phase;	 /**< current_slot()%96 at the in-flight arm (log). */
static uint32_t g_poll_n;	 /**< Count of caught frames (for the throttled log). */

/** @brief Log the first N catches (a full cycle is 15x4=60), then only wins + banners. */
#define CCC_RX_POLL_LOG 700u /* log a full ±HALF cycle so the sweep is visible end-to-end. */

/** @brief Current candidate index: the latched lock, or the live sweep value. */
static uint32_t ccc_rx_cur_cand(void)
{
	return g_locked ? g_lock_cand : g_sweep_cand;
}
#endif /* CCC_RX_LOCK_SWEEP */

// Reset all Per-POLL state: arm count, index tracking, STS warm cache, and optional lock-sweep diagnostic counters; called on entry to a new Pre-POLL listen.
void ccc_shim_rx_log_reset(void)
{
	g_rx_arms = 0u;
	g_have_poll_index = false;
	g_await_poll = false;
	g_armed_index = 0u;
	g_poll_sts_index = 0u;
	g_prepoll_ip = 0u;
	g_poll_stride = 0u;
	g_warm_valid = false;
	g_uad_cached = false;
	g_pp_pending = false;
	g_await_final = false;
	g_poll_ip_for_final = 0u;
	g_final_sts_verdict = -1; /* fail-closed until a Final RFRAME is measured */
	g_final_sts_index = 0;
#if CCC_RX_LOCK_SWEEP
	g_locked = false;
	g_sweep_cand = 0u;
	g_dwell_cnt = 0u;
	g_poll_n = 0u;
#endif
}

// Returns true if the responder is awaiting the POLL frame after a successful Pre-POLL decode.
bool ccc_shim_rx_awaiting_poll(void)
{
	return g_await_poll;
}

// Log one RX event for the optional lock-sweep diagnostic (CONFIG_CCC_RX_LOCK_SWEEP); tracks CPER (STS correlation fail flag) and dwells candidate indices until lock achieved or full cycle exhausted.
void ccc_shim_rx_notify_rx(uint32_t status)
{
#if CCC_RX_LOCK_SWEEP
	unsigned cper;

	if (!ccc_shim_active()) {
		return;
	}
	cper = (status & 0x10000000u) ? 1u : 0u;

	/* Log the first N catches plus every win (CPER=0); ph shows the stable POLL phase. */
	if (cper == 0u || g_poll_n < CCC_RX_POLL_LOG) {
		DIAGK("cat#%u d=%d ph=%u slot=%u csn=%u cper=%u st=%08x\n",
		       (unsigned)g_poll_n, (int)g_cur_delta,
		       (unsigned)g_dbg_phase, (unsigned)g_dbg_slot,
		       (unsigned)fira_session_current_slot(), cper,
		       (unsigned)status);
	}
	g_poll_n++;

	if (g_locked) {
		return;
	}
	/* CPER clear => this frame's true index == the loaded candidate; latch it. Otherwise DWELL then step, announcing a full cycle with no clear. */
	if (cper == 0u) {
		g_locked = true;
		g_lock_cand = g_sweep_cand;
		DIAGK("ccc_rx LOCKED d=%d slot=%u\n",
		       (int)g_cur_delta, (unsigned)g_dbg_slot);
		return;
	}
	if (++g_dwell_cnt >= CCC_RX_DWELL) {
		g_dwell_cnt = 0u;
		if (++g_sweep_cand >= CCC_RX_N_CAND) {
			g_sweep_cand = 0u;
			DIAGK("ccc_rx CYCLE done — all Δ dwelled, no lock\n");
		}
	}
#else
	(void)status;
#endif /* CCC_RX_LOCK_SWEEP */
}

/** BENCH: decode a live SP0 Pre-POLL (CCM*-decrypt with mUPSK1) to read Apple's exact POLL STS index, learn the block stride, and warm the next block's STS. */
#define CCC_RX_PREPOLL_LOG 16u
// Decode a received Pre-POLL frame: verify MHR and message ID, decrypt SP0 payload, cache UAD-derived keys, extract Poll_STS_Index and stride, and pre-warm the next block's STS triplet (POLL, Response_0, Final) to eliminate KDF latency from the critical path.
static void prepoll_decode(const uint8_t *frame, uint16_t datalength)
{
	static uint32_t g_pp_logged;
	struct ccc_mhr_fields mhr;
	// CCC pre-poll message carrying STS mode and hopping schedule start time.
	struct ccc_pre_poll pp;
	const uint8_t *ursk;
	uint8_t plain[CCC_PRE_POLL_LEN];
	int rc;
	bool lg;

	/* Only the verbose trace is budget-limited to the first @c CCC_RX_PREPOLL_LOG. */
	lg = (g_pp_logged < CCC_RX_PREPOLL_LOG);

	if (lg) {
		DIAGK("PREPOLL rx len=%u mhr=", (unsigned)datalength);
		for (unsigned i = 0u; i < CCC_MHR_LEN && (uint16_t)i < datalength; i++) {
			DIAGK("%02x", frame[i]);
		}
		DIAGK("\n");
		g_pp_logged++;
	}

	if (datalength < (uint16_t)(CCC_MHR_LEN + CCC_PRE_POLL_LEN + CCC_SP0_MIC_LEN)) {
		return; /* too short to hold a Pre-POLL */
	}
	if (ccc_parse_mhr(frame, &mhr) != 0) {
		if (lg) {
			DIAGK("PREPOLL mhr parse fail\n");
		}
		return;
	}
	if (mhr.msg_id != CCC_MSG_ID_PRE_POLL) {
		if (lg) {
			DIAGK("PREPOLL not-prepoll msg_id=%02x\n", (unsigned)mhr.msg_id);
		}
		return;
	}
	if (mhr.payload_len != CCC_PRE_POLL_LEN ||
	    (uint16_t)(CCC_MHR_LEN + mhr.payload_len + CCC_SP0_MIC_LEN) > datalength) {
		if (lg) {
			DIAGK("PREPOLL bad len pl=%u dl=%u\n", (unsigned)mhr.payload_len,
			       (unsigned)datalength);
		}
		return;
	}
	ursk = fira_session_get_ursk();
	if (ursk == NULL) {
		return;
	}
	if (!g_uad_cached) {
		uint8_t mupsk2[CCC_MUPSK2_LEN], uad[CCC_UAD_LEN];

		/* Per-session constants (URSK + STS_Index0): derive once, reuse every block so the setup KDFs stay off the decode. */
		ccc_derive_mupsk1(ursk, g_c_mupsk1);
		ccc_derive_mupsk2(ursk, mupsk2);
		ccc_derive_uad(mupsk2, ccc_shim_sts_index0(), uad);
		ccc_uad_addresses(uad, g_c_keysource, g_c_dest, g_c_src_long);
		g_uad_cached = true;
	}
	if (lg) {
		/* Self-check: the UAD-derived DestShort + KeySource must equal the on-air header, else STS_Index0 byte order is wrong. */
		DIAGK("PREPOLL uad dest=%02x%02x ks=%02x%02x%02x%02x | hdr dest=%04x ks=%02x%02x%02x%02x\n",
		       g_c_dest[0], g_c_dest[1], g_c_keysource[0], g_c_keysource[1],
		       g_c_keysource[2], g_c_keysource[3],
		       (unsigned)mhr.dest_short_addr, mhr.key_source[0], mhr.key_source[1],
		       mhr.key_source[2], mhr.key_source[3]);
	}
	rc = ccc_sp0_decrypt(g_c_mupsk1, g_c_src_long, mhr.frame_counter, frame, CCC_MHR_LEN,
			     &frame[CCC_MHR_LEN], mhr.payload_len,
			     &frame[CCC_MHR_LEN + mhr.payload_len], plain);
	if (rc != 0) {
		if (lg) {
			DIAGK("PREPOLL decrypt FAIL rc=%d fc=%u\n", rc,
			       (unsigned)mhr.frame_counter);
		}
		return;
	}
	ccc_pre_poll_parse(plain, &pp);
	if (g_have_poll_index) {
		/* Learn the per-block Poll_STS_Index stride so the POLL-result path can pre-warm the next block's dURSK. */
		g_poll_stride = pp.poll_sts_index - g_poll_sts_index;
	}
	g_poll_sts_index = pp.poll_sts_index; /* ground truth for the next-block prediction */
	g_have_poll_index = true;
	/* Warm the NEXT block's STS now, in this ~190 ms idle, so the next Pre-POLL's SP3 arm packs it with zero KDF on the critical path. */
	if (g_poll_stride != 0u) {
		uint32_t widx = g_poll_sts_index + g_poll_stride;

		/* Warm the POLL (widx), Response_0 (widx+1) AND Final (widx+2) — same round, same dURSK, only STS-V advances — so no leg runs a KDF. */
		if (ccc_shim_sts_for_index(widx, g_warm_dursk, g_warm_sts_v) == 0 &&
		    ccc_shim_sts_for_index(widx + 1u, g_warm_resp_dursk,
					   g_warm_resp_sts_v) == 0 &&
		    ccc_shim_sts_for_index(widx + 2u, g_warm_final_dursk,
					   g_warm_final_sts_v) == 0) {
			g_warm_index = widx;
			g_warm_valid = true;
		}
	}
	if (lg) {
		DIAGK("PREPOLL OK poll_sts_index=%08x cs=%u sts0=%08x\n",
		       (unsigned)pp.poll_sts_index, (unsigned)fira_session_current_slot(),
		       (unsigned)ccc_shim_sts_index0());
	}
}

/** @brief Assemble a 5-byte DW3000 (40-bit) timestamp into a uint64 (DTU ticks). */
static uint64_t ts5_to_u64(const uint8_t t[5])
{
	return (uint64_t)t[0] | ((uint64_t)t[1] << 8) | ((uint64_t)t[2] << 16) |
	       ((uint64_t)t[3] << 24) | ((uint64_t)t[4] << 32);
}

/** Decode a received Final_Data (SP0, msg_id=02): dUDSK-decrypt and parse the initiator's ranging timestamps; not time-critical. */
static void final_data_decode(const uint8_t *frame, uint16_t datalength)
{
	static uint32_t g_fd_logged;
	struct ccc_mhr_fields mhr;
	// CCC final message carrying Aliro authentication data (MAC, derived ranging state).
	struct ccc_final_data fd;
	uint8_t dudsk[CCC_DUDSK_LEN];
	uint8_t plain[64];
	bool lg = (g_fd_logged < 16u);
	int rc;

	if (ccc_parse_mhr(frame, &mhr) != 0 || mhr.msg_id != CCC_MSG_ID_FINAL_DATA) {
		return;
	}
	if (mhr.payload_len > sizeof(plain) ||
	    (uint16_t)(CCC_MHR_LEN + mhr.payload_len + CCC_SP0_MIC_LEN) > datalength) {
		return;
	}
	/* dUDSK for this round's cycle (any in-cycle index works — use the POLL index). */
	if (ccc_shim_dudsk_for_index(g_armed_index, dudsk) != 0) {
		return;
	}
	rc = ccc_sp0_decrypt(dudsk, g_c_src_long, mhr.frame_counter, frame, CCC_MHR_LEN,
			     &frame[CCC_MHR_LEN], mhr.payload_len,
			     &frame[CCC_MHR_LEN + mhr.payload_len], plain);
	if (rc != 0) {
		if (lg) {
			DIAGK("FINALDATA decrypt FAIL rc=%d fc=%u\n", rc,
			       (unsigned)mhr.frame_counter);
			g_fd_logged++;
		}
		return;
	}
	if (ccc_final_data_parse(plain, mhr.payload_len, &fd) != 0 || fd.num_responders == 0u) {
		if (lg) {
			DIAGK("FINALDATA parse FAIL pl=%u\n", (unsigned)mhr.payload_len);
			g_fd_logged++;
		}
		return;
	}
	if (lg) {
		/* final_tx = t5-t1 (POLL->Final tx), r0_ts = t4-t1 (POLL->Response rx) — the two initiator timestamps DS-TWR needs. */
		DIAGK("FINALDATA ok blk=%u final_tx=%u nresp=%u r0_ts=%u\n",
		       (unsigned)fd.ranging_block, (unsigned)fd.ranging_ts_final_tx,
		       (unsigned)fd.num_responders, (unsigned)fd.responders[0].timestamp);
		g_fd_logged++;
	}

	/* DS-TWR: reply1 = Response TX - POLL RX, round2 = Final RX - Response TX; ccc_responder_ds_twr pulls round1/reply2 from the Final_Data. ToF in 15.65 ps ticks. */
	{
		// CCC DS-TWR (double-sided two-way ranging) message carrying poll/response/final timing and STS data.
		struct ccc_ds_twr tw;
		uint32_t t_reply1 = (uint32_t)(g_t_resp_tx - g_t_poll_rx);
		uint32_t t_round2 = (uint32_t)(g_t_final_rx - g_t_resp_tx);

		if (ccc_responder_ds_twr(&fd, 0u, t_reply1, t_round2, &tw) == 0) {
			/* Signed ToF: near zero the numerator goes slightly negative (uint32 would wrap), so compute it signed for bring-up. 1 tick ~ 15.65 ps, ~4.6917 mm/tick. */
			int64_t num = (int64_t)((uint64_t)tw.t_round1 * tw.t_round2) -
				      (int64_t)((uint64_t)tw.t_reply1 * tw.t_reply2);
			int64_t den = (int64_t)tw.t_round1 + tw.t_round2 +
				      tw.t_reply1 + tw.t_reply2;
			int32_t tof = (den != 0) ? (int32_t)(num / den) : 0;
			int d_mm = (int)(((int64_t)tof * 4692) / 1000);
			/* Range-integrity gate (layer 2): the STS-quality floor. Shadow by
			 * default (log the verdict, still latch); define
			 * CONFIG_WOZ_RANGE_GATE_STRICT to drop a failing block instead. Layers 1
			 * (plausibility) and 4 (consensus) live in fira_session below. */
			bool sts_ok = fira_session_sts_quality_ok(g_final_sts_verdict,
								  g_final_sts_index);

			DIAGK("DIST tof=%d d=%dmm rep1=%u rnd2=%u rnd1=%u rep2=%u\n",
			       (int)tof, d_mm,
			       (unsigned)t_reply1, (unsigned)t_round2,
			       (unsigned)tw.t_round1, (unsigned)tw.t_reply2);
			DIAGK("GATE sts=%d verdict=%d sts_ok=%d\n",
			       (int)g_final_sts_index, (int)g_final_sts_verdict,
			       (int)sts_ok);
#if defined(CONFIG_WOZ_PRETTY_SHELL)
			/* Curated one-liner: the per-block distance, gated behind `aliro frames` (default off in pretty) so the console stays quiet unless asked. */
			if (uwb_rxdiag_rng_get()) {
				LOG_INF("rng  blk=%-3u d=%dmm  tof=%d",
					(unsigned)fd.ranging_block, d_mm, (int)tof);
			}
#endif
			/* Feed the range into fira_session_last_range -> UltraWideBandImpl::ReportRange -> AccessManager -> BoltLockMgr -> Matter DoorLock cluster. */
#if defined(CONFIG_WOZ_RANGE_GATE_STRICT)
			if (sts_ok)
#else
			(void)sts_ok;
#endif
				fira_session_set_ccc_range_cm(d_mm / 10, fd.ranging_block);

			/* Consume the per-block capture: the next block must re-stash a
			 * fresh verdict, else a strict gate fails closed. */
			g_final_sts_verdict = -1;
			g_final_sts_index = 0;
		}
	}
}

/**
 * @brief Pre-POLL RX entry (from the RX-good shim): stash the frame and DEFER its ~2 ms
 * decrypt+derive off the Pre-POLL->POLL critical path.
 *
 * The decode only feeds the NEXT block's warm (the SP3 arm keys on the pre-warmed prediction,
 * not this block's fresh index), so it has the ~190 ms idle to run in.  Running it inline —
 * on the single DW3000 workqueue, ahead of the queued POLL RX-OK callback — pushed the
 * Response TX arm past its slot (dx-now < 0 => HPDWARN).  So read the bytes now (cheap SPI),
 * then:
 *   - bootstrap (no warm yet): decode inline to seed the first arm's STS — this block is not
 *     armed, so blocking its POLL is harmless;
 *   - steady state: mark pending; @ref prepoll_rx_rearm runs @ref prepoll_decode after the
 *     Response TX is armed.  A pending decode orphaned by a missed POLL is flushed on the next
 *     Pre-POLL so the warm never goes more than one block stale.
 */
void ccc_shim_rx_try_prepoll(uint16_t datalength)
{
	if (!ccc_shim_active() || datalength == 0u || datalength > sizeof(g_pp_stash)) {
		return; /* (the POLL event is gated out by the shim's await snapshot) */
	}
	if (g_pp_pending) { /* a prior block's POLL-result never ran (missed POLL) — flush it */
		g_pp_pending = false;
		prepoll_decode(g_pp_stash, g_pp_stash_len);
	}
	dwt_readrxdata(g_pp_stash, datalength, 0u); /* stash now; decrypt/derive is deferred */
	g_pp_stash_len = datalength;
	{
		struct ccc_mhr_fields m;

		/* Final_Data (SP0, msg_id=02) carries the initiator's timestamps; decode it INLINE here (never deferred, where its dUDSK decrypt would block the next POLL). */
		if (datalength >= (uint16_t)CCC_MHR_LEN && ccc_parse_mhr(g_pp_stash, &m) == 0 &&
		    m.msg_id == CCC_MSG_ID_FINAL_DATA) {
			final_data_decode(g_pp_stash, datalength);
			return;
		}
	}
	if (!g_warm_valid) {
		prepoll_decode(g_pp_stash, datalength); /* bootstrap: seed the first warm */
	} else {
		g_pp_pending = true;                    /* steady state: decode in the idle */
	}
}

/** @brief Pack a 16-byte `dURSK` into the DW3000 STS-key image (whole-16 reverse). */
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

/** BENCH PROBE (CCC_RX_PACK_SELFTEST) — dump the STS register lanes a known V lands in, to pin the pack_iv byte order. Not shippable. */
#define CCC_RX_PACK_SELFTEST 0 /* answered 2026-07-09: pack_iv needed the reverse (applied). */
#if CCC_RX_PACK_SELFTEST
/** @brief KAT SaltedHash with the POLL index (0x075bcd16) folded BE into [8:11]. */
static const uint8_t ccc_pst_v[CCC_STS_V_LEN] = {
	0x79, 0xe8, 0x65, 0x18, 0x6c, 0xbd, 0x86, 0x4b,
	0x9c, 0xb2, 0x4f, 0xdb, 0x1c, 0xdd, 0x34, 0xf8
};
/** @brief FiRa default Static-STS key — `pack_key` must reproduce the QANI image. */
static const uint8_t ccc_pst_key[CCC_DURSK_LEN] = {
	0x14, 0x14, 0x86, 0x74, 0xd1, 0xd3, 0x36, 0xaa,
	0xf8, 0x60, 0x50, 0xa8, 0x14, 0xeb, 0x22, 0x0f
};

/** @brief Pack a 16-byte V whole-16-reversed then word-LE (the `pack_key`/blob convention). */
static void pack_iv_rev(dwt_sts_cp_iv_t *out, const uint8_t v[CCC_STS_V_LEN])
{
	uint8_t rev[CCC_STS_V_LEN];

	for (size_t i = 0; i < CCC_STS_V_LEN; i++) {
		rev[i] = v[CCC_STS_V_LEN - 1u - i];
	}
	out->iv0 = sys_get_le32(&rev[0]);
	out->iv1 = sys_get_le32(&rev[4]);
	out->iv2 = sys_get_le32(&rev[8]);
	out->iv3 = sys_get_le32(&rev[12]);
}

/** @brief One-shot: dump STS register lanes for the KAT V under three packings. */
static void ccc_pack_selftest(void)
{
	dwt_sts_cp_key_t k;
	dwt_sts_cp_iv_t v;

	/* ANCHOR: pack_key(FiRa default) must read back as the QANI key image, proving reverse+word-LE is the DW3000 KEY order. */
	pack_key(&k, ccc_pst_key);
	dwt_configurestskey(&k);
	DIAGK("PACKST key rd %08x %08x %08x %08x exp 14eb220f f86050a8 d1d336aa 14148674\n",
	       (unsigned)dwt_read_reg(STS_KEY0_REG), (unsigned)dwt_read_reg(STS_KEY1_REG),
	       (unsigned)dwt_read_reg(STS_KEY2_REG), (unsigned)dwt_read_reg(STS_KEY3_REG));
	DIAGK("PACKST V=79e865186cbd864b9cb24fdb1cdd34f8 VCounter=79e86518 idx=075bcd16\n");

	/* A: current word-LE (no reverse) — puts VCounter in iv0, index in iv2. */
	pack_iv(&v, ccc_pst_v);
	__real_dwt_configurestsiv(&v);
	dwt_configurestsloadiv();
	DIAGK("PACKST cur rd %08x %08x %08x %08x ctr %08x\n",
	       (unsigned)dwt_read_reg(STS_IV0_REG), (unsigned)dwt_read_reg(STS_IV1_REG),
	       (unsigned)dwt_read_reg(STS_IV2_REG), (unsigned)dwt_read_reg(STS_IV3_REG),
	       (unsigned)dwt_readctrdbg());

	/* B: whole-16 reverse — puts index in iv1 (spec) but VCounter in iv3 (off-spec). */
	pack_iv_rev(&v, ccc_pst_v);
	__real_dwt_configurestsiv(&v);
	dwt_configurestsloadiv();
	DIAGK("PACKST rev rd %08x %08x %08x %08x ctr %08x\n",
	       (unsigned)dwt_read_reg(STS_IV0_REG), (unsigned)dwt_read_reg(STS_IV1_REG),
	       (unsigned)dwt_read_reg(STS_IV2_REG), (unsigned)dwt_read_reg(STS_IV3_REG),
	       (unsigned)dwt_readctrdbg());

	/* C: raw distinguishable words — readctrdbg names WHICH iv lane is the counter. */
	v.iv0 = 0x00010203u; v.iv1 = 0x04050607u;
	v.iv2 = 0x08090a0bu; v.iv3 = 0x0c0d0e0fu;
	__real_dwt_configurestsiv(&v);
	dwt_configurestsloadiv();
	DIAGK("PACKST raw wr 00010203 04050607 08090a0b 0c0d0e0f ctr %08x <= counter lane\n",
	       (unsigned)dwt_readctrdbg());
}
#endif /* CCC_RX_PACK_SELFTEST */

/** Program the CCC STS for the current ranging slot, then arm RX. */
int32_t __wrap_dwt_rxenable(int32_t mode)
{
	if (ccc_shim_active()) {
#if CCC_RX_PACK_SELFTEST
		/* One-shot STS register-lane dump on the MAC thread; the normal arm below reloads the real STS, so this is transparent. */
		static bool pst_done;

		if (!pst_done) {
			pst_done = true;
			ccc_pack_selftest();
		}
#endif
#if CCC_RX_LOCK_SWEEP
		/* Single-scalar alignment sweep (see CCC_RX_DELTA_SEED): the index is linear (STS_Index0 + elapsed_slots), so load the STS for current_slot() + Δ, swinging outward from the seed. */
		uint32_t cand = ccc_rx_cur_cand();
		uint32_t cs = fira_session_current_slot();
		int32_t  mag = (int32_t)((cand + 1u) / 2u);  /* 0,1,1,2,2,3,3,… */
		int32_t  swing = (cand & 1u) ? mag : -mag;   /* 0,+1,−1,+2,−2,… */
		int32_t  delta = CCC_RX_DELTA_SEED + swing;  /* seed + outward swing */
		uint32_t slot = (uint32_t)((int32_t)cs + delta);

		g_cur_delta = delta;
		g_dbg_phase = cs % CCC_RX_SLOTS_PER_BLOCK;
		g_dbg_slot = slot;
#elif CCC_RX_PIN_ROUND0_POLL
		uint32_t slot = 1u; /* STS_Index0 + 1 = round-0 POLL */
#else
		uint32_t slot = fira_session_current_slot();
#endif
		uint8_t dursk[CCC_DURSK_LEN], sts_v[CCC_STS_V_LEN];

		if (ccc_shim_sts_for_slot(slot, dursk, sts_v) == 0) {
			dwt_sts_cp_key_t k;
			dwt_sts_cp_iv_t v;

			pack_key(&k, dursk);
			pack_iv(&v, sts_v);
			dwt_configurestskey(&k);       /* unwrapped -> direct */
			__real_dwt_configurestsiv(&v); /* bypass the TX IV wrap */
			dwt_configurestsloadiv();      /* reset the HW STS counter to our IV */
#if CCC_RX_FORCE_SP3
			/* The blob armed SP0 (STS engine off); flip CP_SPC to SP3/ND so the STS runs on the POLL. Direct __real_ call to skip the rxdiag wrap. */
			__real_dwt_configurestsmode((uint8_t)DWT_STS_MODE_ND);
#endif

			/* Synchronous printk (survives deferred-log starvation); rd==wr proves the key reached the register, slot proves the index clock advances. */
			if (g_rx_arms < CCC_RX_LOG_ARMS) {
				DIAGK("ccc_rx arm#%u slot=%u key0 wr %08x rd %08x iv2=%08x\n",
				       (unsigned)g_rx_arms, (unsigned)slot, k.key0,
				       (unsigned)dwt_read_reg(STS_KEY0_REG), v.iv2);
			}
			g_rx_arms++;
		}
	}
	return __real_dwt_rxenable(mode);
}

#if WOZ_CCC_PREPOLL_LISTEN
/** Re-arm the SP0 receiver after each RX event; the rxdiag --wrap=dwt_setcallbacks shim runs ccc_shim_rx_try_prepoll first, so here we keep the plain SP0 receive listening. */
#define CCC_RX_DIAG_N 110u /**< per-catch lines to emit (~1 catch/block now — spans a full sweep). */

/** Sleep between wakes, hi32 (~4 ns) units: one 192 ms block MINUS ~5 slots, waking just ahead of the Pre-POLL so it is caught fresh each block. */
#define CCC_RX_SLEEP_HI32 45900000u

/** @brief One 192 ms ranging block, hi32 (~4 ns) units (measured on air, dsys ≈ 47.92 M). */
#define CCC_RX_BLOCK_HI32 47920000u
/** @brief One 2 ms slot, hi32 units (measured, dsys ≈ 499 000). */
#define CCC_RX_SLOT_HI32 499000u
/** @brief Anchored SP0 RX window length, dwt_setrxtimeout units (1.0256 µs): ~1 slot, so a miss times out before the POLL slot. */
#define CCC_RX_WIN_TO 1950u
/** @brief Sweep the armed offset ±2 slots (0..4 -> -2..+2), so an off-by-one gap does not read as "no Pre-POLL". */
#define CCC_RX_SWEEP_N 5u

/** @brief Open the SP3 POLL window this many hi32 units (~320 µs) BEFORE the POLL RMARKER (Pre-POLL + 1 slot), leaving settle time so a too-late window doesn't preamble-miss. */
/* Was 150000 (~600 µs): only ~68 µs arm margin so spikes dropped blocks; 80000 (~320 µs) widens the margin to ~350 µs. */
#define CCC_RX_POLL_LEAD 80000u
/** @brief SP3 POLL RX window (dwt_setrxtimeout units, 1.0256 µs): ~1.4 ms, delayed to the POLL slot, opening ~600 µs early. */
#define CCC_RX_POLL_WIN_TO 1350u

/** @brief try_prepoll() decode duration (hi32 ~4 ns units), reported on the ARM-FAIL line to attribute the pre-arm latency. */
extern uint32_t g_ccc_dbg_decode;

/** @brief Listen-gate: true only while the Pre-POLL listener is up. ccc_prepoll_stop() closes it so no callback rearm can re-enable RX after a session stop; ccc_prepoll_listen() reopens it before its arm. */
static volatile bool g_listen_gate;

/** @brief Gate-checked RX arm for every self-rearm site below; refuses once the listen-gate is closed. */
static int32_t gated_rxenable(int32_t mode)
{
	if (!g_listen_gate) {
		return (int32_t)DWT_ERROR;
	}
	return __real_dwt_rxenable(mode);
}

/** Flip to SP3/ND, load the pre-warmed CCC STS (g_warm_index), and arm a delayed RX to catch the POLL that follows the Pre-POLL. */
static int arm_poll_sp3(uint32_t prepoll_ip)
{
	static uint32_t dbg_n;
	uint32_t t0 = k_cycle_get_32();
	const uint8_t *pd, *pv;
	unsigned warm;
	dwt_sts_cp_key_t k;
	dwt_sts_cp_iv_t v;

	/* The STS for the predicted index was pre-derived in the idle (g_warm_*); pack it with no KDF on the critical path. No warm yet => skip. */
	if (!g_warm_valid) {
		return -EIO;
	}
	warm = 1u;
	pd = g_warm_dursk;
	pv = g_warm_sts_v;
	/* Commit the Response_0 (idx+1) AND Final (idx+2) STS NOW, before this block's decode re-warms them to the next index. */
	for (size_t i = 0; i < CCC_DURSK_LEN; i++) {
		g_armed_resp_dursk[i] = g_warm_resp_dursk[i];
		g_armed_final_dursk[i] = g_warm_final_dursk[i];
	}
	for (size_t i = 0; i < CCC_STS_V_LEN; i++) {
		g_armed_resp_sts_v[i] = g_warm_resp_sts_v[i];
		g_armed_final_sts_v[i] = g_warm_final_sts_v[i];
	}
	pack_key(&k, pd);
	pack_iv(&v, pv);
	dwt_configurestskey(&k);                               /* unwrapped -> direct */
	__real_dwt_configurestsiv(&v);                         /* bypass the TX IV wrap */
	dwt_configurestsloadiv();                              /* reset HW STS counter to our IV */
	__real_dwt_configurestsmode((uint8_t)DWT_STS_MODE_ND); /* SP0 -> SP3/ND for the POLL */

	/* Probe (first 8 arms): armlat = arm compute in cycles; warm = fast path taken; ctr must equal iv0, a match proving the load is clean. */
	if (dbg_n < 8u) {
		uint32_t cyc = k_cycle_get_32() - t0;
		uint32_t ctr = dwt_readctrdbg();
		uint32_t cc = dwt_read_reg(CHAN_CTRL_REG);
		unsigned rxcode = (unsigned)((cc >> 8) & 0x1Fu);
		unsigned sfd = (unsigned)((cc >> 1) & 0x3u);
		unsigned spc = (unsigned)((dwt_read_reg(SYS_CFG_REG) >> 12) & 0x3u);
		unsigned slen = (unsigned)(dwt_read_reg(STS_CFG0_REG) & 0xFFu);

		/* Prove the ACTIVE SP3 PHY at the arm: rxcode=9, sfd=0 (4a), spc=3 (SP3/ND), slen=7 (STS64); a mismatch floors stsq regardless of key. */
		DIAGK("STSDBG idx=%08x warm=%u armlat=%uc rxcode=%u sfd=%u spc=%u slen=%u ctr=%08x iv0exp=%02x%02x%02x%02x vidx=%02x%02x%02x%02x\n",
		       (unsigned)g_warm_index, warm, (unsigned)cyc, rxcode, sfd, spc, slen, (unsigned)ctr,
		       pv[12], pv[13], pv[14], pv[15],
		       pv[8], pv[9], pv[10], pv[11]);
		dbg_n++;
	}

	g_prepoll_ip = prepoll_ip; /* anchor for the POLL-result gap (`d=`) log */
	/* Pin the window to the POLL slot (Pre-POLL + 1 slot).  The arm is sub-µs
	 * (armlat ~20 cyc), so this delayed target is safely in the future; a "late"
	 * error just means we lost the block — revert to SP0 (IDLE_ON_DLY_ERR keeps
	 * the RX from re-enabling immediately and catching foreign traffic). */
	/* dsys = RMARKER->arm gap in hi32 (4 ns) units; the delayed target must still be
	 * in the future when rxenable latches it (dsys < SLOT - LEAD), else HPDWARN. */
	uint32_t dsys = dwt_readsystimestamphi32() - prepoll_ip;
	dwt_setdelayedtrxtime(prepoll_ip + CCC_RX_SLOT_HI32 - CCC_RX_POLL_LEAD);
	dwt_setrxtimeout(CCC_RX_POLL_WIN_TO);
	if (gated_rxenable(DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR) != DWT_SUCCESS) {
		/* dsys >= (SLOT - LEAD) => "late": the DELAYED RX never opened, so rxto
		 * stays 0 and the POLL is lost.  Log the first few to size the gap. */
		static uint32_t arm_fail_n;
		if (arm_fail_n < 40u) {
			DIAGK("ARM FAIL dsys=%u(%dus) dec=%dus off=%u %s idx=%08x\n",
			       (unsigned)dsys, (int)(dsys / 250u), (int)(g_ccc_dbg_decode / 250u),
			       (unsigned)(CCC_RX_SLOT_HI32 - CCC_RX_POLL_LEAD),
			       (dsys >= (CCC_RX_SLOT_HI32 - CCC_RX_POLL_LEAD)) ? "LATE" : "not-late",
			       (unsigned)g_warm_index);
			arm_fail_n++;
		}
		__real_dwt_configurestsmode((uint8_t)DWT_STS_MODE_OFF); /* revert to SP0 */
		return -EIO;
	}
	return 0;
}

/** @brief Revert SP3/ND -> SP0 and re-arm the permanent Pre-POLL listen (no timeout). */
static void revert_to_sp0_listen(void)
{
	dwt_forcetrxoff(); /* a refused delayed TX leaves the sequencer pending — clear it first */
	__real_dwt_configurestsmode((uint8_t)DWT_STS_MODE_OFF);
	dwt_setrxtimeout(0u);
	(void)gated_rxenable(DWT_START_RX_IMMEDIATE);
}

/** @brief Dummy Response_0 body — NOT radiated (SP3/ND sends STS only), but the TX sequence writes a frame body before dwt_starttx. */
static uint8_t g_resp_payload[4];

/** Delayed-TX the responder's Response_0 (SP3-ND) one slot after the POLL, at STS index Poll_STS_Index + 1 (same dURSK, STS-V advances). */
static int tx_response_sp3(uint32_t poll_ip, uint32_t resp_idx)
{
	static uint32_t dbg_n;
	dwt_sts_cp_key_t k;
	dwt_sts_cp_iv_t v;
	uint32_t dx, now;
	int r;

	/* NO KDF here — pack the Response STS committed at the arm (g_armed_resp_*); the derive already ran in the idle. resp_idx is only for the diagnostic print. */
	pack_key(&k, g_armed_resp_dursk);
	pack_iv(&v, g_armed_resp_sts_v);
	dwt_configurestskey(&k);       /* same dURSK as the POLL round */
	__real_dwt_configurestsiv(&v); /* Response STS-V (index+1); bypass the TX IV wrap */
	dwt_configurestsloadiv();      /* reset the HW STS counter to our V */
	/* STS mode stays SP3/ND (the POLL arm set it) — the Response is the same RFRAME. */
	dx = poll_ip + CCC_RX_SLOT_HI32; /* RMARKER = POLL + 1 slot (hi32, same domain as poll_ip) */
	dwt_setdelayedtrxtime(dx);
	dwt_writetxdata(sizeof(g_resp_payload), g_resp_payload, 0u);
	dwt_writetxfctrl(sizeof(g_resp_payload) + 2u, 0u, 1u); /* +FCS; ranging=1 (STS) */
	now = dwt_readsystimestamphi32();
	r = dwt_starttx(DWT_START_TX_DELAYED);
	if (dbg_n < 8u) {
		/* PROBE (removable): dx-now = margin to the programmed TX RMARKER (hi32/4 ns); negative => the target was already past (HPDWARN reject). */
		uint32_t ss = dwt_readsysstatuslo();

		DIAGK("RESPTX r=%d dx-now=%d(%dus) hpd=%u ss=%08x idx=%08x\n",
		       r, (int32_t)(dx - now), (int32_t)(dx - now) / 250,
		       (ss & DWT_INT_HPDWARN_BIT_MASK) ? 1u : 0u, (unsigned)ss,
		       (unsigned)resp_idx);
		dbg_n++;
	}
	return (r == DWT_SUCCESS) ? 0 : -EIO;
}

/** Arm the delayed SP3-ND RX for the phone's Final (POLL + 2 slots) at STS index Poll_STS_Index+2, packing the g_armed_final_* STS (no KDF). */
static int arm_final_sp3(uint32_t poll_ip)
{
	static uint32_t dbg_n;
	dwt_sts_cp_key_t k;
	dwt_sts_cp_iv_t v;
	uint32_t dx, now;
	int r;

	pack_key(&k, g_armed_final_dursk);
	pack_iv(&v, g_armed_final_sts_v);
	dwt_configurestskey(&k);
	__real_dwt_configurestsiv(&v);
	dwt_configurestsloadiv();
	dx = poll_ip + 2u * CCC_RX_SLOT_HI32; /* Final RMARKER = POLL + 2 slots */
	now = dwt_readsystimestamphi32();
	dwt_setdelayedtrxtime(dx - CCC_RX_POLL_LEAD);
	dwt_setrxtimeout(CCC_RX_POLL_WIN_TO);
	r = gated_rxenable(DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR);
	if (dbg_n < 8u) {
		DIAGK("FINALARM r=%d dx-now=%d(%dus) idx=%08x\n",
		       r, (int32_t)((dx - CCC_RX_POLL_LEAD) - now),
		       (int32_t)((dx - CCC_RX_POLL_LEAD) - now) / 250,
		       (unsigned)(g_armed_index + 2u));
		dbg_n++;
	}
	return (r == DWT_SUCCESS) ? 0 : -EIO;
}

/** TX-done (TXFRS) callback: our Response_0 left the antenna, so arm the Final RX one slot later, then run the block's deferred Pre-POLL decode in the idle. */
static void resp_tx_done(const dwt_cb_data_t *cb)
{
	static uint32_t g_resp_txn;

	(void)cb;
	if (g_resp_txn < 16u) {
		DIAGK("RESP txdone #%u idx=%08x\n",
		       (unsigned)g_resp_txn, (unsigned)(g_armed_index + 1u));
		g_resp_txn++;
	}
	{
		uint8_t txts[5] = { 0 };

		dwt_readtxtimestamp(txts); /* t3: responder Response TX (antenna plane) */
		g_t_resp_tx = ts5_to_u64(txts);
	}
	if (arm_final_sp3(g_poll_ip_for_final) == 0) {
		g_await_final = true;
	} else {
		revert_to_sp0_listen();
	}
	/* Deferred Pre-POLL decode (warms the NEXT block's STS) — after the time-critical Final arm, in the ~190 ms idle. */
	if (g_pp_pending) {
		g_pp_pending = false;
		prepoll_decode(g_pp_stash, g_pp_stash_len);
	}
}

// RX callback for Pre-POLL listen and POLL/Final results: re-arm SP0 by default, or arm SP3/ND for POLL if a warmed index is ready, or fire the delayed-TX Response_0 and Final RX arm on valid POLL CPER; log free-running timing and optionally defer Pre-POLL decode to warm the next block.
static void prepoll_rx_rearm(const dwt_cb_data_t *cb)
{
	/* FREE-RUNNING TIMING+CONTENT DIAGNOSTIC (de-starved: re-arm first, log deferred): free-run and log each real frame's hardware Ipatov RX timestamp + delta, to validate the true slot/block timing. */
	static uint32_t prev_ip;
	static uint32_t g_cia;
	static uint32_t g_dumps;
	uint32_t st = (cb != NULL) ? cb->status : 0u;
	uint32_t ip = 0u;
	uint64_t ip40 = 0u;
	uint16_t len = 0u;
	uint8_t  rng = 0u;
	bool     is_pp = false;
	uint8_t  b[CCC_MHR_LEN] = { 0 };

	if ((st & DWT_INT_CIADONE_BIT_MASK) != 0u) {
		uint8_t tsb[5] = { 0 };

		dwt_readrxtimestamp_ipatov(tsb); /* hardware RX ts -- precise, CPU-independent */
		ip40 = ts5_to_u64(tsb);          /* full 40-bit DTU for DS-TWR */
		ip = (uint32_t)(ip40 >> 8);      /* bits[39:8] — the delayed-TRX / arm domain */
	}
	if ((st & DWT_INT_RXPHD_BIT_MASK) != 0u) {
		// CCC MAC header fields (source/dest addresses, frame control, sequence number).
		struct ccc_mhr_fields m;

		dwt_readrxdata(b, sizeof(b), 0u);
		len = dwt_getframelength(&rng);
		/* Arm only on a CLEANLY-received, GENUINE Pre-POLL: require RXFCG (good CRC) too, else a Final_Data sharing the Pre-POLL MHR framing fires a spurious arm. */
		is_pp = ((st & DWT_INT_RXFCG_BIT_MASK) != 0u &&
			 len >= (uint16_t)CCC_MHR_LEN && ccc_parse_mhr(b, &m) == 0 &&
			 m.msg_id == CCC_MSG_ID_PRE_POLL);
	}

	/* Re-arm. Default: keep listening in SP0. After a decoded Pre-POLL, arm the STS receiver (SP3/ND) for the POLL; while pending (g_await_poll) the next RX event is the POLL result. */
	if (g_await_final) {
		unsigned cper = (st & 0x10000000u) ? 1u : 0u;
		int d = (ip != 0u) ? (int)(ip - g_poll_ip_for_final) : 0;
		int16_t stsq = 0;
		int qret = 0;

		g_await_final = false;
		if (ip != 0u) {
			g_t_final_rx = ip40; /* t6: responder Final RX */
			qret = dwt_readstsquality(&stsq, 0);
			/* Range-integrity gate (layer 2): stash this Final RFRAME's STS
			 * verdict for the Final_Data decode that computes the distance. */
			g_final_sts_verdict = qret;
			g_final_sts_index = stsq;
		}
		/* Final result (DS-TWR leg 3): cper=0 => the idx+2 STS correlated; ip is the responder's third timestamp, d = Final - POLL ~= 2 slots. */
		DIAGK("FINAL result st=%08x cper=%u ip=%08x d=%d(%dus) stsq=%d/%d idx=%08x\n",
		       (unsigned)st, cper, (unsigned)ip, d, d / 250, (int)stsq, qret,
		       (unsigned)(g_armed_index + 2u));
		revert_to_sp0_listen();
	} else if (g_await_poll) {
		unsigned cper = (st & 0x10000000u) ? 1u : 0u;
		int d = (ip != 0u) ? (int)(ip - g_prepoll_ip) : 0;
		int16_t stsq = 0;
		int qret = 0;
		int tr = -1;

		g_await_poll = false;
		/* Time-critical FIRST: arm Response_0's delayed TX before the stsq read and printk. cper=0 => real POLL, so delayed-TX Response_0 (index+1); else return to the SP0 listen. */
		if (cper == 0u && ip != 0u) {
			g_poll_ip_for_final = ip; /* round anchor for the Final RX arm (TXDONE) */
			g_t_poll_rx = ip40;       /* t2: responder POLL RX */
			tr = tx_response_sp3(ip, g_armed_index + 1u);
		}
		if (tr != 0) {
			revert_to_sp0_listen();
		}
		/* else: stay SP3/ND; resp_tx_done arms the Final RX, then reverts to SP0. */

		/* Deferred diagnostics (off the TX critical path): stsq splits the cper=1 cause (low = key/IV wrong, high-but-clipped = saturation). */
		if (ip != 0u) {
			qret = dwt_readstsquality(&stsq, 0);
		}
		DIAGK("POLL result st=%08x cper=%u d=%d(%dus) stsq=%d/%d idx=%08x resp=%s dec=%dus\n",
		       (unsigned)st, cper, d, d / 250, (int)stsq, qret, (unsigned)g_armed_index,
		       (cper == 0u && ip != 0u) ? ((tr == 0) ? "armed" : "FAIL") : "-",
		       (int)(g_ccc_dbg_decode / 250u));

		/* Deferred Pre-POLL decode (warms the NEXT block's STS): on a Response-sent block resp_tx_done runs it; otherwise run it here in the idle. */
		if (tr != 0 && g_pp_pending) {
			g_pp_pending = false;
			prepoll_decode(g_pp_stash, g_pp_stash_len);
		}
	} else if (is_pp && g_warm_valid && ip != 0u &&
		   g_warm_index != g_armed_index) {
		/* Arm the POLL window on the PREDICTED next index (warmed a block ago), one arm per index; mark it before arming so a re-detected Pre-POLL can't re-arm late. */
		g_armed_index = g_warm_index;
		if (arm_poll_sp3(ip) == 0) {
			g_await_poll = true; /* SP3 armed; do not re-arm SP0 */
		} else {
			dwt_setrxtimeout(0u);
			(void)gated_rxenable(DWT_START_RX_IMMEDIATE);
		}
	} else {
		dwt_setrxtimeout(0u);
		(void)gated_rxenable(DWT_START_RX_IMMEDIATE);
	}

	if (ip != 0u && g_cia < 64u) {
		DIAGK("cia#%u ip=%08x dip=%d st=%08x len=%u%s\n",
		       (unsigned)g_cia, (unsigned)ip, (int)(ip - prev_ip),
		       (unsigned)st, (unsigned)len, is_pp ? " <<49 2b PRE-POLL>>" : "");
		g_cia++;
	}
	if (ip != 0u) {
		prev_ip = ip;
	}
	if ((st & DWT_INT_RXPHD_BIT_MASK) != 0u && g_dumps < 16u) {
		/* Cap the dump (was unbounded on every is_pp): the per-block phd flood backs up the workqueue, delaying the Pre-POLL callback. A fixed cap suffices. */
		DIAGK("  phd%s %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
		       is_pp ? " <<49 2b>>" : "",
		       b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
		       b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
		g_dumps++;
	}
}

/** Sync (preamble) code to listen on for the SP0 Pre-POLL: 9, read from the phone's plaintext M4 (07 01 09 = SYNC_Code_Index=9). */
#define CCC_RX_PREPOLL_CODE 9u

// Initialize the DW3000 radio for permanent SP0 Pre-POLL listen: configure PHY (6.8 Mbps, preamble length 64, SFD 4a, no STS), install RX callbacks that self-rearm on every frame outcome, and enable all RX/TX interrupts; returns 0 on success.
int ccc_prepoll_listen(uint8_t channel, uint8_t preamble_code)
{
	dwt_config_t cfg = {
		.chan           = channel,
		/* PHY: CCC negotiates one PHY set per session, shared by SP0 (Pre-POLL) and SP3 (POLL). Config 0 (cfg=0000) pins the default preamble at 64 symbols, so the set is plen64 + the phone's SYNC_Code_Index (code 9). */
		.txPreambLength = DWT_PLEN_64,
		.rxPAC          = DWT_PAC8,
		.txCode         = CCC_RX_PREPOLL_CODE,
		.rxCode         = CCC_RX_PREPOLL_CODE,
		.sfdType        = DWT_SFD_IEEE_4A,  /* Config 0000 SFD = ternary 8-sym (4a), not 4z; 4z SFD-timed-out every phone frame */
		.dataRate       = DWT_BR_6M8,       /* Pre-POLL PSDU @ 6.81 Mbps */
		.phrMode        = DWT_PHRMODE_STD,
		.phrRate        = DWT_PHRRATE_STD,  /* Pre-POLL PHR @ 850 kbps */
		.sfdTO          = 64 + 1,           /* numeric preamble length + 1 */
		.stsMode        = DWT_STS_MODE_OFF, /* SP0 — data frame, PHR+payload, no STS */
		.stsLength      = DWT_STS_LEN_64,
		.pdoaMode       = DWT_PDOA_M0,
	};
	dwt_callbacks_s cbs = {
		.cbRxOk   = prepoll_rx_rearm,
		.cbRxTo   = prepoll_rx_rearm,
		.cbRxErr  = prepoll_rx_rearm,
		.cbTxDone = resp_tx_done, /* Response_0 TXFRS -> revert to SP0 listen */
	};
	int rc = uwb_min_radio_init();

	if (rc != 0) {
		DIAGK("prepoll_listen: radio init failed (%d)\n", rc);
		return rc;
	}
	dwt_forcetrxoff();
	if (dwt_configure(&cfg) != DWT_SUCCESS) {
		DIAGK("prepoll_listen: dwt_configure failed\n");
		return -EIO;
	}
	/* Permanent listen: no RX timeout; the callbacks self-re-arm so a missed/errored frame doesn't stop the search for the next Pre-POLL. */
	dwt_setrxtimeout(0u);
	dwt_setcallbacks(&cbs); /* rxdiag --wrap installs shim_rxok -> ccc_shim_rx_try_prepoll */
	dwt_setinterrupt(DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXFCE_BIT_MASK |
			 DWT_INT_RXFTO_BIT_MASK | DWT_INT_RXPTO_BIT_MASK |
			 DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXSTO_BIT_MASK |
			 DWT_INT_RXFSL_BIT_MASK | DWT_INT_ARFE_BIT_MASK |
			 DWT_INT_TXFRS_BIT_MASK, /* Response_0 TX-done -> resp_tx_done */
			 0u, DWT_ENABLE_INT);
	ccc_shim_rx_log_reset();
	DIAGK("prepoll_listen: SP0 RX up (ch=%u code=%u plen64 sts=off; sp0code=%u) — listening for Apple Pre-POLL\n",
	       (unsigned)channel, (unsigned)CCC_RX_PREPOLL_CODE, (unsigned)preamble_code);
	g_listen_gate = true; /* reopen the listen-gate a prior ccc_prepoll_stop() closed */
	(void)__real_dwt_rxenable(DWT_START_RX_IMMEDIATE);
	return 0;
}

/* Stop the permanent Pre-POLL listener: close the listen-gate (every self-rearm
 * site checks it via gated_rxenable), then force the radio out of RX/TX.  The
 * DW3000 callbacks run on the dedicated coop (-11) isr workqueue with
 * busy-polled SPI and synchronous printk, so a callback never yields
 * mid-flight: one in flight when a preemptive-thread caller gets here has
 * already run to completion (its rearm landed BEFORE our forcetrxoff), and any
 * later callback sees the gate closed.  A residual rearm window exists only if
 * this is ever called from an ISR or a coop thread at prio <= -11. */
void ccc_prepoll_stop(void)
{
	if (!g_listen_gate) {
		return; /* never started or already stopped — the driver may be unprobed, so no SPI */
	}
	g_listen_gate = false; /* order matters: close the gate, then kill RX */
	dwt_forcetrxoff();
}
#endif /* WOZ_CCC_PREPOLL_LISTEN */
