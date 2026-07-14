/** @file test_ccc_session.c — Aliro session -> ccc_ran_params mapping. */
#include <errno.h>

#include "ccc_session.h"
#include "test.h"

static struct ccc_ran_session mk(void)
{
	struct ccc_ran_session s = {
		.uwb_session_id = 0x11u,
		.sts_index0 = 0x1000u,
		.uwb_time0 = 0u,
		.hop_key_rw = 0x2222u,
		.mac_mode_offset = 0u,
		.n_ran_s = 8u,
		.n_chap_per_slot = 4u,
		.n_responder = 1u,
		.n_slot_per_round = 6u,
		.hop_mode = CCC_HOP_CONTINUOUS,
	};
	return s;
}

void test_ccc_session(void)
{
	struct ccc_ran_session s = mk();
	struct ccc_ran_params p;

	t_group("n_round formula");
	/* 288 * n_ran_s / (n_chap_per_slot * n_slot_per_round) = 288*8/(4*6) = 96. */
	T_EQ("n_round", ccc_session_n_round(&s), 96u);
	T_EQ("n_round.null", ccc_session_n_round(NULL), 0u);
	struct ccc_ran_session z = mk();
	z.n_chap_per_slot = 0u; /* zero denominator */
	T_EQ("n_round.zero_denom", ccc_session_n_round(&z), 0u);

	t_group("to_ran_params mapping");
	T_EQ("map.ok", ccc_session_to_ran_params(&s, &p), 0);
	T_EQ("map.sts0", p.sts_index0, s.sts_index0);
	T_EQ("map.nslot", p.n_slot_per_round, s.n_slot_per_round);
	T_EQ("map.nround", p.n_round, 96u);
	T_EQ("map.nresp", p.n_responder, s.n_responder);
	T_EQ("map.hopkey", p.hop_key_rw, s.hop_key_rw);
	T_EQ("map.hopmode", p.hop_mode, s.hop_mode);

	t_group("to_ran_params errors");
	T_EQ("map.null.s", ccc_session_to_ran_params(NULL, &p), -EINVAL);
	T_EQ("map.null.out", ccc_session_to_ran_params(&s, NULL), -EINVAL);
	/* A round must hold N_Responder + 4 packets. */
	struct ccc_ran_session tight = mk();
	tight.n_slot_per_round = tight.n_responder + 3u; /* one short */
	T_EQ("map.slots_short", ccc_session_to_ran_params(&tight, &p), -EINVAL);
	/* n_round == 0 (denominator overflow of the ratio) also rejects. */
	struct ccc_ran_session norounds = mk();
	norounds.n_ran_s = 0u; /* 288*0 / d = 0 */
	T_EQ("map.zero_round", ccc_session_to_ran_params(&norounds, &p), -EINVAL);
}
