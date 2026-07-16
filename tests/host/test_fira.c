/** @file test_fira.c — range + URSK store (fira_session). */
#include <string.h>

#include "aliro_kdf.h" /* ALIRO_URSK_LEN */
#include "fira_session.h"
#include "test.h"

void test_fira(void)
{
	uint8_t ursk[ALIRO_URSK_LEN];
	int32_t cm = -1;
	uint32_t block = 0;
	int64_t age = -1;

	for (size_t i = 0; i < sizeof(ursk); i++) {
		ursk[i] = (uint8_t)(i + 1u);
	}

	t_group("range store starts empty");
	fira_session_set_provisioned_ursk(NULL); /* also normalises state */
	T_OK("empty", !fira_session_last_range(&cm, NULL, NULL, NULL, NULL));

	t_group("URSK stash");
	T_OK("ursk.none", fira_session_get_ursk() == NULL);
	fira_session_set_provisioned_ursk(ursk);
	const uint8_t *g = fira_session_get_ursk();
	T_OK("ursk.set", g != NULL && memcmp(g, ursk, sizeof(ursk)) == 0);
	fira_session_set_provisioned_ursk(NULL);
	T_OK("ursk.cleared", fira_session_get_ursk() == NULL);

	t_group("diagnostic slot counter is inert");
	T_EQ("slot", fira_session_current_slot(), 0u);

	t_group("latch + read range");
	fira_session_set_ccc_range_cm(150, 7u);
	T_OK("present", fira_session_last_range(&cm, NULL, NULL, &block, &age));
	T_EQ("cm", cm, 150);
	T_EQ("block", block, 7u);
	T_OK("age.nonneg", age >= 0);
	/* Negative distance clamps to 0. */
	fira_session_set_ccc_range_cm(-5, 9u);
	fira_session_last_range(&cm, NULL, NULL, &block, NULL);
	T_EQ("cm.clamped", cm, 0);
	T_EQ("block2", block, 9u);
	/* All out-params optional. */
	T_OK("null.outs", fira_session_last_range(NULL, NULL, NULL, NULL, NULL));

	/* ── Range-integrity gate ─────────────────────────────────────────────── */

	t_group("layer 1: plausibility predicate");
	T_OK("plaus.150", fira_session_range_plausible(150));
	T_OK("plaus.zero", fira_session_range_plausible(0));
	T_OK("plaus.small_neg", fira_session_range_plausible(-FIRA_RANGE_NEG_TOL_CM));
	T_OK("plaus.max", fira_session_range_plausible(FIRA_RANGE_MAX_CM));
	T_OK("implaus.big_neg", !fira_session_range_plausible(-400));
	T_OK("implaus.below_tol", !fira_session_range_plausible(-FIRA_RANGE_NEG_TOL_CM - 1));
	T_OK("implaus.over_max", !fira_session_range_plausible(FIRA_RANGE_MAX_CM + 1));

	t_group("layer 1: Ghost-Peak negative is dropped, not clamped to 0");
	fira_session_set_ccc_range_cm(120, 20u); /* a good close read */
	fira_session_last_range(&cm, NULL, NULL, &block, NULL);
	T_EQ("good.latched", cm, 120);
	/* The reported exploit: a hugely negative ToF must NOT latch as a valid 0 cm. */
	fira_session_set_ccc_range_cm(-400, 21u);
	fira_session_last_range(&cm, NULL, NULL, &block, NULL);
	T_EQ("exploit.not_zero", cm, 120);   /* kept last good; did NOT become 0 */
	T_EQ("exploit.block_kept", block, 20u); /* the spoof block never latched */
	/* An over-envelope outlier is likewise dropped. */
	fira_session_set_ccc_range_cm(FIRA_RANGE_MAX_CM + 500, 22u);
	fira_session_last_range(&cm, NULL, NULL, NULL, NULL);
	T_EQ("overmax.dropped", cm, 120);
	/* Small negative is legit point-blank slop and still reads as 0. */
	fira_session_set_ccc_range_cm(-3, 23u);
	fira_session_last_range(&cm, NULL, NULL, NULL, NULL);
	T_EQ("small_neg.zero", cm, 0);

	t_group("layer 2: STS-quality floor");
	T_OK("sts.good", fira_session_sts_quality_ok(0, 100));
	T_OK("sts.good_at_floor", fira_session_sts_quality_ok(5, FIRA_STS_QUALITY_MIN));
	T_OK("sts.bad_verdict", !fira_session_sts_quality_ok(-1, 100));
	T_OK("sts.below_floor",
	     !fira_session_sts_quality_ok(0, (int16_t)(FIRA_STS_QUALITY_MIN - 1)));

	t_group("layer 3: first-path integrity");
	/* Strong LOS: first-path power dominates -> small ratio -> accept. */
	T_OK("fp.los_ok", fira_session_first_path_ok(1000, 900, 800, 2));
	/* Injected/weak first path: tiny F, large channel power -> ratio blows up. */
	T_OK("fp.nlos_reject", !fira_session_first_path_ok(10, 10, 10, 100000));
	/* No first path at all -> reject. */
	T_OK("fp.zero_reject", !fira_session_first_path_ok(0, 0, 0, 5000));
	/* Realistic DW3000 Ipatov magnitudes (~2^20) must not overflow the
	 * f1^2+f2^2+f3^2 accumulation and still classify a strong LOS path. */
	T_OK("fp.realistic_los",
	     fira_session_first_path_ok(1u << 20, 1u << 20, 1u << 20, 2u));

	t_group("layer 4: cross-block consensus builds and breaks trust");
	fira_session_set_ccc_range_cm(90, 30u);
	fira_session_set_ccc_range_cm(92, 31u); /* within SPREAD of 90 */
	T_OK("trust.building", !fira_session_range_trusted()); /* only 2 agreeing */
	fira_session_set_ccc_range_cm(91, 32u); /* 3rd agreeing -> K reached */
	T_OK("trust.reached", fira_session_range_trusted());
	/* A lone plausible outlier breaks trust but still latches (live telemetry). */
	fira_session_set_ccc_range_cm(300, 33u); /* jump > SPREAD */
	fira_session_last_range(&cm, NULL, NULL, NULL, NULL);
	T_EQ("outlier.latched", cm, 300);
	T_OK("outlier.untrusted", !fira_session_range_trusted());
	/* Rebuild trust, then a spoof (implausible) block clears it and does not latch. */
	fira_session_set_ccc_range_cm(305, 34u);
	fira_session_set_ccc_range_cm(307, 35u);
	fira_session_set_ccc_range_cm(303, 36u);
	T_OK("trust.rebuilt", fira_session_range_trusted());
	fira_session_set_ccc_range_cm(-500, 37u); /* Ghost-Peak */
	T_OK("spoof.clears_trust", !fira_session_range_trusted());
	fira_session_last_range(&cm, NULL, NULL, &block, NULL);
	T_EQ("spoof.range_kept", cm, 303);
	T_EQ("spoof.block_kept", block, 36u);

	t_group("layer 4: trust saturates at K and an outlier forces a full rebuild");
	fira_session_set_ccc_range_cm(200, 40u);
	fira_session_set_ccc_range_cm(202, 41u);
	fira_session_set_ccc_range_cm(201, 42u); /* K reached */
	fira_session_set_ccc_range_cm(203, 43u); /* extra agreeing: trust caps at K */
	fira_session_set_ccc_range_cm(199, 44u);
	T_OK("sat.trusted", fira_session_range_trusted());
	/* One jump > SPREAD must drop trust to a single fresh sample, so a lone
	 * agreeing block after it is NOT enough — a full K-run has to rebuild. */
	fira_session_set_ccc_range_cm(400, 45u); /* jump -> run restarts at 1 */
	T_OK("sat.jump.untrusted", !fira_session_range_trusted());
	fira_session_set_ccc_range_cm(402, 46u); /* 2 */
	T_OK("sat.one_after_jump_insufficient", !fira_session_range_trusted());
	fira_session_set_ccc_range_cm(401, 47u); /* 3 -> K reached again */
	T_OK("sat.full_rebuild_trusted", fira_session_range_trusted());
}
