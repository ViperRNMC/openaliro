/** @file test_prepoll_round.c — one full DS-TWR round through the real
 * listener: genuinely CCM*-encrypted Pre-POLLs are decoded (bootstrap + stride
 * learning + STS warm), the SP3 POLL window is armed off the warm, the POLL
 * result fires the Response TX, TXFRS arms the Final, and the Final_Data
 * decrypt latches a range into fira_session. Frames are built with the same
 * public codec + KDF the initiator side would use, so the decrypt is real. */
#include <string.h>

#include <deca_device_api.h>

#include "aliro_kdf.h" /* ALIRO_URSK_LEN */
#include "ccc_kdf.h"
#include "ccc_mac.h"
#include "ccc_shim.h"
#include "fira_session.h"
#include "woz_uwb_facade.h"
#include "test.h"

/* The CCC STS substitution wrap — callable directly on the host (no ld --wrap). */
extern int32_t __wrap_dwt_rxenable(int32_t mode);

#define RND_SID  0x11223344u
#define RND_STS0 0x00400000u
#define RND_IDX1 5000u   /* first Pre-POLL's Poll_STS_Index */
#define RND_STRIDE 96u   /* per-block index stride the decode must learn */
#define RND_BLOCK 7u

/* Session crypto constants, derived in setup exactly as prepoll_decode does. */
static uint8_t g_ursk[ALIRO_URSK_LEN];
static uint8_t g_mupsk1[CCC_MUPSK1_LEN];
static uint8_t g_ks[CCC_KEYSOURCE_LEN];
static uint8_t g_dest[CCC_DEST_SHORT_ADDR_LEN];
static uint8_t g_src_long[CCC_SRC_LONG_ADDR_LEN];

/** Build an encrypted Pre-POLL frame; returns its on-air length. */
static uint16_t mk_prepoll(uint8_t *out, uint32_t fc, uint32_t poll_idx)
{
	struct ccc_mhr_fields f;
	struct ccc_pre_poll pp;
	uint8_t plain[CCC_PRE_POLL_LEN];

	memset(&pp, 0, sizeof(pp));
	pp.uwb_session_id = RND_SID;
	pp.poll_sts_index = poll_idx;
	pp.ranging_block = RND_BLOCK;
	ccc_pre_poll_pack(&pp, plain);

	memset(&f, 0, sizeof(f));
	f.dest_short_addr = (uint16_t)(g_dest[0] | ((uint16_t)g_dest[1] << 8));
	f.frame_counter = fc;
	memcpy(f.key_source, g_ks, CCC_KEYSOURCE_LEN);
	f.msg_id = CCC_MSG_ID_PRE_POLL;
	f.payload_len = CCC_PRE_POLL_LEN;
	T_EQ("mk_pp.mhr", ccc_build_mhr(&f, out), 0);
	T_EQ("mk_pp.enc",
	     ccc_sp0_encrypt(g_mupsk1, g_src_long, fc, out, CCC_MHR_LEN, plain,
			     CCC_PRE_POLL_LEN, &out[CCC_MHR_LEN],
			     &out[CCC_MHR_LEN + CCC_PRE_POLL_LEN]),
	     0);
	return CCC_MHR_LEN + CCC_PRE_POLL_LEN + CCC_SP0_MIC_LEN;
}

/** Build an encrypted Final_Data (1 responder) keyed on the armed POLL index. */
static uint16_t mk_final_data(uint8_t *out, uint32_t fc, uint32_t armed_idx,
			      uint32_t t_round1, uint32_t t_reply2)
{
	struct ccc_mhr_fields f;
	struct ccc_final_data fd;
	uint8_t plain[64];
	uint8_t dudsk[CCC_DUDSK_LEN];
	size_t pl = 0;

	memset(&fd, 0, sizeof(fd));
	fd.uwb_session_id = RND_SID;
	fd.ranging_block = RND_BLOCK;
	fd.final_sts_index = armed_idx + 2u;
	fd.ranging_ts_final_tx = t_round1 + t_reply2; /* t5-t1 */
	fd.num_responders = 1u;
	fd.responders[0].timestamp = t_round1;        /* t4-t1 */
	T_EQ("mk_fd.pack", ccc_final_data_pack(&fd, plain, sizeof(plain), &pl), 0);

	memset(&f, 0, sizeof(f));
	f.dest_short_addr = (uint16_t)(g_dest[0] | ((uint16_t)g_dest[1] << 8));
	f.frame_counter = fc;
	memcpy(f.key_source, g_ks, CCC_KEYSOURCE_LEN);
	f.msg_id = CCC_MSG_ID_FINAL_DATA;
	f.payload_len = (uint8_t)pl;
	T_EQ("mk_fd.mhr", ccc_build_mhr(&f, out), 0);
	T_EQ("mk_fd.key", ccc_shim_dudsk_for_index(armed_idx, dudsk), 0);
	T_EQ("mk_fd.enc",
	     ccc_sp0_encrypt(dudsk, g_src_long, fc, out, CCC_MHR_LEN, plain, pl,
			     &out[CCC_MHR_LEN], &out[CCC_MHR_LEN + pl]),
	     0);
	return (uint16_t)(CCC_MHR_LEN + pl + CCC_SP0_MIC_LEN);
}

/** Load a frame + Ipatov timestamp into the stub, then feed it to try_prepoll. */
static void stash_frame(const uint8_t *frame, uint16_t len, uint64_t ip40)
{
	memcpy(woz_host_rx.rxdata, frame, len);
	woz_host_rx.rxdata_len = len;
	woz_host_rx.rx_ts40 = ip40;
}

/** Fire a captured RX callback the way dwt_isr would. */
static void rx_event(dwt_cb_t cb, uint32_t status)
{
	dwt_cb_data_t d;

	memset(&d, 0, sizeof(d));
	d.status = status;
	d.datalength = woz_host_rx.rxdata_len;
	cb(&d);
}

/* Good-frame status: CIA done (timestamp valid) + PHR + CRC good. */
#define ST_GOOD (DWT_INT_CIADONE_BIT_MASK | DWT_INT_RXPHD_BIT_MASK | \
		 DWT_INT_RXFCG_BIT_MASK)
#define ST_CPER 0x10000000u /* STS correlation error (matches the in-tree literal) */

void test_prepoll_round(void)
{
	uint8_t frame[128];
	uint16_t len;
	uint8_t rc[17];
	struct woz_uwb_aliro_cfg c;
	uint8_t mupsk2[CCC_MUPSK2_LEN], uad[CCC_UAD_LEN];
	const uint32_t widx = RND_IDX1 + 2u * RND_STRIDE; /* warmed POLL index */
	uint32_t fc = 100u;
	int32_t cm = -1;

	t_group("session setup mirrors the initiator's derivations");
	for (size_t i = 0; i < sizeof(g_ursk); i++) {
		g_ursk[i] = (uint8_t)(0xA0u + i);
	}
	for (size_t i = 0; i < sizeof(rc); i++) {
		rc[i] = (uint8_t)i;
	}
	T_EQ("kdf.mupsk1", ccc_derive_mupsk1(g_ursk, g_mupsk1), 0);
	T_EQ("kdf.mupsk2", ccc_derive_mupsk2(g_ursk, mupsk2), 0);
	T_EQ("kdf.uad", ccc_derive_uad(mupsk2, RND_STS0, uad), 0);
	T_EQ("kdf.addr", ccc_uad_addresses(uad, g_ks, g_dest, g_src_long), 0);

	memset(&c, 0, sizeof(c));
	c.session_id = RND_SID;
	c.channel = 9u;
	c.sync_code_index = 9u;
	c.slot_per_round = 12u;
	c.sts_index0 = RND_STS0;
	c.ursk = g_ursk;
	c.ranging_config = rc;
	c.rc_len = sizeof(rc);
	woz_host_rx_reset();
	T_EQ("start", woz_uwb_start_aliro(&c), 0);
	T_EQ("start.armed", woz_host_rx.rxenable_calls, 1);

	t_group("STS substitution wrap programs a key while bound");
	T_EQ("wrap.rxenable", __wrap_dwt_rxenable(DWT_START_RX_IMMEDIATE),
	     DWT_SUCCESS);

	t_group("bootstrap: two Pre-POLL decodes learn index + stride");
	len = mk_prepoll(frame, fc++, RND_IDX1);
	stash_frame(frame, len, 0x1000000ull);
	ccc_shim_rx_try_prepoll(len); /* inline decode #1 — index, no stride */
	len = mk_prepoll(frame, fc++, RND_IDX1 + RND_STRIDE);
	stash_frame(frame, len, 0x2000000ull);
	ccc_shim_rx_try_prepoll(len); /* inline decode #2 — stride, warms widx */

	t_group("Pre-POLL event arms the SP3 POLL window off the warm");
	T_OK("prearm.not_awaiting", !ccc_shim_rx_awaiting_poll());
	stash_frame(frame, len, 0x3000000ull); /* MHR re-read by the callback */
	rx_event(woz_host_rx.cbs.cbRxOk, ST_GOOD);
	T_OK("arm.awaiting_poll", ccc_shim_rx_awaiting_poll());
	T_EQ("arm.delayed", woz_host_rx.last_rxenable_mode,
	     DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR);
	/* Next block's Pre-POLL arrives while armed: stash + defer its decode. */
	len = mk_prepoll(frame, fc++, RND_IDX1 + 2u * RND_STRIDE);
	stash_frame(frame, len, 0x3100000ull);
	ccc_shim_rx_try_prepoll(len);

	t_group("POLL result (cper=0) fires the delayed Response TX");
	woz_host_rx.rx_ts40 = 0x40000000ull;            /* t2: POLL RX */
	rx_event(woz_host_rx.cbs.cbRxOk, DWT_INT_CIADONE_BIT_MASK);
	T_EQ("poll.resp_tx", woz_host_rx.starttx_calls, 1);
	T_OK("poll.await_cleared", !ccc_shim_rx_awaiting_poll());

	t_group("TXFRS arms the Final window and flushes the deferred decode");
	woz_host_rx.tx_ts40 = 0x40000000ull + 100000u;  /* t3 = t2 + 100k DTU */
	rx_event(woz_host_rx.cbs.cbTxDone, DWT_INT_TXFRS_BIT_MASK);
	T_EQ("final.armed", woz_host_rx.last_rxenable_mode,
	     DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR);

	t_group("Final result stashes the STS verdict, reverts to SP0");
	woz_host_rx.rx_ts40 = 0x40000000ull + 300000u;  /* t6 = t3 + 200k DTU */
	woz_host_rx.stsq_ret = 0;
	woz_host_rx.stsq_val = 100;
	rx_event(woz_host_rx.cbs.cbRxOk, DWT_INT_CIADONE_BIT_MASK);
	T_EQ("final.sp0", woz_host_rx.last_rxenable_mode, DWT_START_RX_IMMEDIATE);

	t_group("Final_Data decrypt latches the DS-TWR range");
	/* reply1=100k, round2=200k (injected above); round1=101k, reply2=199k
	 * => tof = (101k*200k - 100k*199k) / 600k = 500 ticks = 234 cm. */
	len = mk_final_data(frame, fc++, widx, 101000u, 199000u);
	stash_frame(frame, len, 0x3200000ull);
	ccc_shim_rx_try_prepoll(len); /* Final_Data decodes inline */
	T_OK("range.latched", fira_session_last_range(&cm, NULL, NULL, NULL, NULL));
	T_EQ("range.cm", cm, 234);

	t_group("round 2: POLL result with STS error reverts and reflushes");
	stash_frame(frame, len, 0x4000000ull);
	len = mk_prepoll(frame, fc++, RND_IDX1 + 3u * RND_STRIDE);
	stash_frame(frame, len, 0x4000000ull);
	rx_event(woz_host_rx.cbs.cbRxOk, ST_GOOD);    /* re-arm off decode #3's warm */
	T_OK("arm2.awaiting", ccc_shim_rx_awaiting_poll());
	len = mk_prepoll(frame, fc++, RND_IDX1 + 4u * RND_STRIDE);
	stash_frame(frame, len, 0x4100000ull);
	ccc_shim_rx_try_prepoll(len);                 /* pending decode #4 */
	rx_event(woz_host_rx.cbs.cbRxOk, DWT_INT_CIADONE_BIT_MASK | ST_CPER);
	T_OK("poll2.no_tx", woz_host_rx.starttx_calls == 1); /* no new Response */
	T_EQ("poll2.sp0", woz_host_rx.last_rxenable_mode, DWT_START_RX_IMMEDIATE);

	t_group("a refused delayed arm falls back to the SP0 listen");
	stash_frame(frame, len, 0x5000000ull);
	woz_host_rx.rxenable_ret = DWT_ERROR;
	rx_event(woz_host_rx.cbs.cbRxOk, ST_GOOD);    /* arm fails -> ARM FAIL path */
	T_OK("armfail.not_awaiting", !ccc_shim_rx_awaiting_poll());
	woz_host_rx.rxenable_ret = DWT_SUCCESS;

	/* Quiet the radio for any suite that follows. */
	woz_uwb_stop();
}
