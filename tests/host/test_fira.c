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
}
