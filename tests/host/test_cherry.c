/** @file test_cherry.c — cherry_ccc_shim: context/session lifecycle + setters. */
#include <stdlib.h>
#include <string.h>

#include <cherry/cherry.h>
#include <cherry/cherry_ccc.h>
#include <cherry/cherry_session.h>

#include "test.h"

/* Capture the last CCC event the shim emits, then release it. */
static struct {
	int count;
	enum cherry_ccc_event_type type;
	enum cherry_ccc_session_state state;
	enum cherry_err err;
} g_cc;

static void cc_cb(struct cherry_ccc_event *ev, void *user_data)
{
	(void)user_data;
	g_cc.count++;
	g_cc.type = ev->type;
	if (ev->type == CHERRY_CCC_EVENT_TYPE_SESSION_STATUS) {
		g_cc.state = ev->data.status->session_state;
	} else if (ev->type == CHERRY_CCC_EVENT_TYPE_SESSION_ERROR) {
		g_cc.err = ev->data.error->status_err;
	}
	cherry_ccc_event_free(ev);
}

void test_cherry(void)
{
	struct cherry_ccc_aliro_session_config cfg;
	struct cherry_common_diag_cfg diag;
	uint8_t ursk[32];

	memset(&g_cc, 0, sizeof(g_cc));
	memset(&diag, 0, sizeof(diag));
	for (size_t i = 0; i < sizeof(ursk); i++) {
		ursk[i] = (uint8_t)i;
	}

	t_group("cherry context create");
	struct cherry *cx = cherry_create("host", NULL, NULL);
	T_OK("create", cx != NULL);

	t_group("create_aliro_responder guards + happy");
	memset(&cfg, 0, sizeof(cfg));
	cfg.session_id = 0x11223344u;
	cfg.uwb_config_id = 0x0001u;
	cfg.channel = 5u;
	cfg.sync_code_index = 9u;
	cfg.ranging_duration_ms = 768u;
	cfg.slot_per_rr = 6u;
	cfg.slot_duration = 1200u;
	cfg.sts_index = 0x1000u;
	cfg.pulse_shape_combo = 0u;
	T_OK("resp.null.cb",
	     cherry_ccc_session_create_aliro_responder(cx, NULL, NULL, &cfg) == NULL);
	T_OK("resp.null.cfg",
	     cherry_ccc_session_create_aliro_responder(cx, cc_cb, NULL, NULL) == NULL);
	struct cherry_ccc_session *s =
		cherry_ccc_session_create_aliro_responder(cx, cc_cb, (void *)0x1234,
							  &cfg);
	T_OK("resp.ok", s != NULL);

	t_group("base up-cast + user_data");
	struct cherry_session *base = cherry_ccc_session_to_base(s);
	T_OK("base", base != NULL);
	T_OK("udata", cherry_session_get_user_data(base) == (void *)0x1234);
	T_OK("udata.null", cherry_session_get_user_data(NULL) == NULL);

	t_group("start rejected without URSK / null session");
	T_EQ("start.noursk", cherry_session_start(base), CHERRY_ERR_SESSION_CONFIG);
	T_EQ("start.null", cherry_session_start(NULL), CHERRY_ERR_INVALID_PARAMETER);

	t_group("set_ursk guards + happy");
	T_EQ("ursk.null.sess", cherry_ccc_session_set_ursk(NULL, ursk),
	     CHERRY_ERR_INVALID_PARAMETER);
	T_EQ("ursk.null.key", cherry_ccc_session_set_ursk(s, NULL),
	     CHERRY_ERR_INVALID_PARAMETER);
	T_EQ("ursk.ok", cherry_ccc_session_set_ursk(s, ursk), CHERRY_ERR_NONE);

	t_group("start drives INIT -> IDLE -> ACTIVE");
	g_cc.count = 0;
	T_EQ("start.ok", cherry_session_start(base), CHERRY_ERR_NONE);
	T_EQ("start.events", g_cc.count, 2);
	T_EQ("start.active", g_cc.state, CHERRY_CCC_SESSION_STATE_ACTIVE);

	t_group("stop emits IDLE / null session");
	g_cc.count = 0;
	T_EQ("stop.ok", cherry_session_stop(base), CHERRY_ERR_NONE);
	T_EQ("stop.idle", g_cc.state, CHERRY_CCC_SESSION_STATE_IDLE);
	T_EQ("stop.null", cherry_session_stop(NULL), CHERRY_ERR_INVALID_PARAMETER);

	t_group("fine setters (write-through + null guards)");
	T_EQ("proto.ok", cherry_ccc_session_set_protocol_version(s, 0x0100u),
	     CHERRY_ERR_NONE);
	T_EQ("proto.null", cherry_ccc_session_set_protocol_version(NULL, 0x0100u),
	     CHERRY_ERR_INVALID_PARAMETER);
	T_EQ("sts.ok", cherry_ccc_session_set_sts_index(s, 0x2000u), CHERRY_ERR_NONE);
	T_EQ("sts.wrote", cfg.sts_index, 0x2000u);
	T_EQ("sts.null", cherry_ccc_session_set_sts_index(NULL, 0u),
	     CHERRY_ERR_INVALID_PARAMETER);
	T_EQ("time.ok", cherry_ccc_session_set_initiation_time(s, 12345u),
	     CHERRY_ERR_NONE);
	T_EQ("time.wrote", (long)cfg.uwb_time_us, 12345);
	T_EQ("time.null", cherry_ccc_session_set_initiation_time(NULL, 0u),
	     CHERRY_ERR_INVALID_PARAMETER);
	T_EQ("r2.ok", cherry_ccc_session_set_round2_antennas(s, 1u, 2u),
	     CHERRY_ERR_NONE);
	T_EQ("r2.null", cherry_ccc_session_set_round2_antennas(NULL, 1u, 2u),
	     CHERRY_ERR_INVALID_PARAMETER);
	T_EQ("ant.ok", cherry_ccc_session_set_antennas(s, 1u, 2u), CHERRY_ERR_NONE);
	T_EQ("ant.null", cherry_session_set_antennas(NULL, 1u, 2u),
	     CHERRY_ERR_INVALID_PARAMETER);
	T_EQ("diag.ok", cherry_ccc_session_set_diagnostics(s, diag), CHERRY_ERR_NONE);
	T_EQ("diag.null", cherry_session_set_diagnostics(NULL, diag, false),
	     CHERRY_ERR_INVALID_PARAMETER);

	t_group("event_free variants");
	cherry_ccc_event_free(NULL);
	struct cherry_ccc_event *e1 = malloc(sizeof(*e1));
	e1->type = CHERRY_CCC_EVENT_TYPE_SESSION_STATUS;
	e1->data.status = malloc(sizeof(*e1->data.status));
	cherry_ccc_event_free(e1);
	struct cherry_ccc_event *e2 = malloc(sizeof(*e2));
	e2->type = CHERRY_CCC_EVENT_TYPE_SESSION_ERROR;
	e2->data.error = malloc(sizeof(*e2->data.error));
	cherry_ccc_event_free(e2);
	struct cherry_ccc_event *e3 = malloc(sizeof(*e3));
	e3->type = CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLLER_REPORT;
	cherry_ccc_event_free(e3);
	T_OK("event_free.survived", 1);

	t_group("destroy emits DEINIT + frees; null is a no-op");
	g_cc.count = 0;
	cherry_ccc_session_destroy(s);
	T_EQ("destroy.deinit", g_cc.state, CHERRY_CCC_SESSION_STATE_DEINIT);
	cherry_session_destroy(NULL);

	cherry_destroy_sync(cx);
}
