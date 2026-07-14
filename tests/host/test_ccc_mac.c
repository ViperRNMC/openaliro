/** @file test_ccc_mac.c — MAC codec, ranging schedule, DS-TWR, URSK lifetime. */
#include <errno.h>
#include <string.h>

#include "ccc_mac.h"
#include "test.h"

static void hopping(void)
{
	t_group("hop round index");
	t_u16("hop.idx", ccc_hop_round_index(5u, 0x12345678u, 8u), "0007");
	/* Round is always within [0, n_round). */
	for (uint32_t i = 0; i < 64; i++) {
		T_OK("hop.in_range", ccc_hop_round_index(i, 0xdeadbeefu, 12u) < 12u);
	}
}

static void mhr_codec(void)
{
	struct ccc_mhr_fields in = {
		.dest_short_addr = 0xbeefu,
		.frame_counter = 0x11223344u,
		.key_source = { 0x01, 0x02, 0x03, 0x04 },
		.msg_id = CCC_MSG_ID_PRE_POLL,
		.payload_len = CCC_PRE_POLL_LEN,
	};
	struct ccc_mhr_fields out;
	uint8_t buf[CCC_MHR_LEN];

	t_group("MHR pack/parse round-trip");
	T_EQ("mhr.build", ccc_build_mhr(&in, buf), 0);
	T_EQ("mhr.parse", ccc_parse_mhr(buf, &out), 0);
	T_EQ("mhr.dest", out.dest_short_addr, in.dest_short_addr);
	T_EQ("mhr.ctr", out.frame_counter, in.frame_counter);
	T_OK("mhr.ks", memcmp(out.key_source, in.key_source, CCC_KEYSOURCE_LEN) == 0);
	T_EQ("mhr.msgid", out.msg_id, in.msg_id);
	T_EQ("mhr.plen", out.payload_len, in.payload_len);

	t_group("MHR errors");
	T_EQ("mhr.build.null", ccc_build_mhr(NULL, buf), -EINVAL);
	T_EQ("mhr.parse.null", ccc_parse_mhr(NULL, &out), -EINVAL);
	buf[0] ^= 0xffu; /* corrupt frame control */
	T_EQ("mhr.parse.badfc", ccc_parse_mhr(buf, &out), -EINVAL);
}

static void pre_poll_codec(void)
{
	struct ccc_pre_poll in = {
		.uwb_session_id = 0xaabbccddu,
		.poll_sts_index = 0x00ff00ffu,
		.ranging_block = 0x1234u,
		.hop_flag = 1u,
		.round_index = 0x0007u,
	};
	struct ccc_pre_poll out;
	uint8_t buf[CCC_PRE_POLL_LEN];

	t_group("Pre-POLL pack/parse round-trip");
	T_EQ("pp.pack", ccc_pre_poll_pack(&in, buf), 0);
	T_EQ("pp.parse", ccc_pre_poll_parse(buf, &out), 0);
	T_EQ("pp.sid", out.uwb_session_id, in.uwb_session_id);
	T_EQ("pp.sts", out.poll_sts_index, in.poll_sts_index);
	T_EQ("pp.block", out.ranging_block, in.ranging_block);
	T_EQ("pp.hop", out.hop_flag, in.hop_flag);
	T_EQ("pp.round", out.round_index, in.round_index);
	T_EQ("pp.pack.null", ccc_pre_poll_pack(NULL, buf), -EINVAL);
	T_EQ("pp.parse.null", ccc_pre_poll_parse(buf, NULL), -EINVAL);
}

static void final_data_codec(void)
{
	struct ccc_final_data in;
	struct ccc_final_data out;
	uint8_t buf[128];
	size_t len = 0;

	memset(&in, 0, sizeof(in));
	in.uwb_session_id = 0x01020304u;
	in.ranging_block = 0x0042u;
	in.hop_flag = 1u;
	in.round_index = 0x0003u;
	in.final_sts_index = 0x00abcdefu;
	in.ranging_ts_final_tx = 0x00998877u;
	in.num_responders = 2u;
	in.responders[0] = (struct ccc_responder_ts){ 0u, 0x1000u, 3u, 1u };
	in.responders[1] = (struct ccc_responder_ts){ 1u, 0x2000u, 4u, 0u };

	t_group("Final_Data pack/parse round-trip");
	T_EQ("fd.pack", ccc_final_data_pack(&in, buf, sizeof(buf), &len), 0);
	T_EQ("fd.len", len, CCC_FINAL_DATA_HDR_LEN + 2u * CCC_RESPONDER_LEN);
	T_EQ("fd.parse", ccc_final_data_parse(buf, len, &out), 0);
	T_EQ("fd.sid", out.uwb_session_id, in.uwb_session_id);
	T_EQ("fd.final_tx", out.ranging_ts_final_tx, in.ranging_ts_final_tx);
	T_EQ("fd.nresp", out.num_responders, in.num_responders);
	T_EQ("fd.r1.ts", out.responders[1].timestamp, in.responders[1].timestamp);
	T_EQ("fd.r1.status", out.responders[1].ranging_status,
	     in.responders[1].ranging_status);

	t_group("Final_Data errors");
	T_EQ("fd.pack.null", ccc_final_data_pack(NULL, buf, sizeof(buf), &len), -EINVAL);
	in.num_responders = CCC_MAX_RESPONDERS + 1u;
	T_EQ("fd.pack.toomany", ccc_final_data_pack(&in, buf, sizeof(buf), &len), -EINVAL);
	in.num_responders = 2u;
	T_EQ("fd.pack.smallcap", ccc_final_data_pack(&in, buf, 4u, &len), -EINVAL);
	T_EQ("fd.parse.short", ccc_final_data_parse(buf, 3u, &out), -EINVAL);
	T_EQ("fd.parse.badlen", ccc_final_data_parse(buf, len - 1u, &out), -EINVAL);
}

static void schedule(void)
{
	struct ccc_ran_params none = {
		.sts_index0 = 0x1000u, .n_slot_per_round = 6u, .n_round = 8u,
		.n_responder = 2u, .hop_key_rw = 0u, .hop_mode = CCC_HOP_NONE,
	};
	struct ccc_ran_params cont = none;
	cont.hop_key_rw = 0x12345678u;
	cont.hop_mode = CCC_HOP_CONTINUOUS;

	t_group("block round + slot STS index");
	T_EQ("round.block0", ccc_block_round(&cont, 0u), 0); /* block 0 never hops */
	T_EQ("round.none", ccc_block_round(&none, 5u), 0);   /* no-hop stays 0 */
	uint16_t r = ccc_block_round(&cont, 5u);
	t_u16("round.cont", r, "0007");
	t_u32("slot.response", ccc_slot_sts_index(&cont, 5u, r, CCC_SLOT_RESPONSE, 1u),
	      "0000111d");
	/* Every slot role yields a distinct, ordered offset within a round. */
	uint32_t pre = ccc_slot_sts_index(&none, 0u, 0u, CCC_SLOT_PRE_POLL, 0u);
	uint32_t poll = ccc_slot_sts_index(&none, 0u, 0u, CCC_SLOT_POLL, 0u);
	uint32_t fin = ccc_slot_sts_index(&none, 0u, 0u, CCC_SLOT_FINAL, 0u);
	uint32_t fdat = ccc_slot_sts_index(&none, 0u, 0u, CCC_SLOT_FINAL_DATA, 0u);
	T_EQ("slot.pre", pre, none.sts_index0);
	T_EQ("slot.poll", poll, none.sts_index0 + 1u);
	T_OK("slot.order", poll < fin && fin < fdat);
	T_EQ("round.null", ccc_block_round(NULL, 5u), 0);
	T_EQ("slot.null", ccc_slot_sts_index(NULL, 0u, 0u, CCC_SLOT_POLL, 0u), 0);

	t_group("initiator next hop");
	struct ccc_hop_decision dn = ccc_initiator_next_hop(&none, 5u);
	T_EQ("nexthop.none.flag", dn.hop_flag, 0);
	struct ccc_hop_decision dc = ccc_initiator_next_hop(&cont, 5u);
	T_EQ("nexthop.cont.flag", dc.hop_flag, 1);
}

static void ds_twr(void)
{
	struct ccc_ds_twr t = {
		.t_round1 = 0x00030000u, .t_reply1 = 0x00010000u,
		.t_round2 = 0x00028000u, .t_reply2 = 0x00018000u,
	};
	struct ccc_ds_twr zero = { 0u, 0u, 0u, 0u };
	struct ccc_final_data fd;

	t_group("DS-TWR time-of-flight");
	t_u32("tof", ccc_ds_twr_tof(&t), "0000c000");
	T_EQ("tof.zero_den", ccc_ds_twr_tof(&zero), 0);
	T_EQ("tof.null", ccc_ds_twr_tof(NULL), 0);

	t_group("responder DS-TWR assembly");
	memset(&fd, 0, sizeof(fd));
	fd.num_responders = 1u;
	fd.ranging_ts_final_tx = 0x00050000u;
	fd.responders[0].timestamp = 0x00030000u;
	struct ccc_ds_twr out;
	T_EQ("resp.ok", ccc_responder_ds_twr(&fd, 0u, 0x00010000u, 0x00028000u, &out), 0);
	T_EQ("resp.round1", out.t_round1, fd.responders[0].timestamp);
	T_EQ("resp.oob", ccc_responder_ds_twr(&fd, 5u, 0u, 0u, &out), -EINVAL);
	T_EQ("resp.null", ccc_responder_ds_twr(NULL, 0u, 0u, 0u, &out), -EINVAL);
}

static void ursk_lifetime(void)
{
	struct ccc_ran_params p = {
		.sts_index0 = 0u, .n_slot_per_round = 6u, .n_round = 8u,
		.n_responder = 2u, .hop_key_rw = 0u, .hop_mode = CCC_HOP_NONE,
	};
	struct ccc_ran_params late = p;
	late.sts_index0 = CCC_STS_INDEX_MAX - 10u; /* one more span overflows the cap */

	t_group("URSK exhaustion");
	T_OK("ursk.fresh", !ccc_ursk_exhausted(&p, 0u));
	T_OK("ursk.exhausted", ccc_ursk_exhausted(&late, 1u));
	T_OK("ursk.null", ccc_ursk_exhausted(NULL, 0u));
}

void test_ccc_mac(void)
{
	hopping();
	mhr_codec();
	pre_poll_codec();
	final_data_codec();
	schedule();
	ds_twr();
	ursk_lifetime();
}
