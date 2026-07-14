/** @file test_facade.c — woz_uwb_facade: bind/start/stop + range readback. */
#include <errno.h>
#include <string.h>

#include "aliro_kdf.h" /* ALIRO_URSK_LEN */
#include "ccc_shim.h"
#include "fira_session.h"
#include "woz_uwb_facade.h"
#include "test.h"

void test_facade(void)
{
	uint8_t ursk[ALIRO_URSK_LEN];
	uint8_t rc[17];
	struct woz_uwb_aliro_cfg c;
	int32_t cm = -1;

	for (size_t i = 0; i < sizeof(ursk); i++) {
		ursk[i] = (uint8_t)(i + 1u);
	}
	for (size_t i = 0; i < sizeof(rc); i++) {
		rc[i] = (uint8_t)i;
	}

	t_group("bind_ursk binds the CCC shim");
	T_OK("shim.unbound.before", !ccc_shim_active());
	T_EQ("bind.ok", woz_uwb_bind_ursk(ursk, sizeof(ursk)), 0);
	T_OK("shim.bound.after", ccc_shim_active());

	t_group("start_aliro rejects null cfg / null ursk");
	T_EQ("start.null", woz_uwb_start_aliro(NULL), -EINVAL);
	memset(&c, 0, sizeof(c));
	c.ursk = NULL;
	T_EQ("start.null.ursk", woz_uwb_start_aliro(&c), -EINVAL);

	t_group("start_aliro with a serialized RangingConfiguration");
	memset(&c, 0, sizeof(c));
	c.session_id = 0x11223344u;
	c.channel = 5u;
	c.sync_code_index = 9u;
	c.slot_duration_rstu = 1200u;
	c.block_duration_ms = 768u;
	c.slot_per_round = 6u;
	c.sts_index0 = 0x1000u;
	c.uwb_time_us = 0u;
	c.ursk = ursk;
	c.ranging_config = rc;
	c.rc_len = sizeof(rc);
	T_EQ("start.rc", woz_uwb_start_aliro(&c), 0);
	T_OK("shim.active.rc", ccc_shim_active());

	t_group("start_aliro URSK fallback (no ranging_config, slot_per_round 0)");
	c.ranging_config = NULL;
	c.rc_len = 0u;
	c.slot_per_round = 0u;
	T_EQ("start.fallback", woz_uwb_start_aliro(&c), 0);

	t_group("stop unbinds the shim");
	woz_uwb_stop();
	T_OK("shim.unbound", !ccc_shim_active());

	t_group("last_range_cm reflects the fira range store");
	fira_session_set_ccc_range_cm(150, 3u);
	T_OK("range.present", woz_uwb_last_range_cm(&cm));
	T_EQ("range.cm", cm, 150);

	/* Leave the fira store cleared for any later reader. */
	fira_session_set_provisioned_ursk(NULL);
}
