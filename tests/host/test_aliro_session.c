/** @file test_aliro_session.c — session lifecycle + state machine + dispatch. */
#include <stdlib.h>
#include <string.h>

#include "aliro_uwb_internal.h" /* struct aliro_uwb_session, states, internal fns */
#include "aliro_uwb_msg.h"      /* header accessors */
#include "aliro_uwb_msg_builder.h"
#include "aliro_uwb_msg_spec.h"
#include "test.h"

/* --- transmit + event capture ------------------------------------------- */

static struct {
	struct aliro_uwb_message *msg;
	bool timeout;
	int count;
} g_tx;

static void tx_cb(struct aliro_uwb_message *m, struct aliro_uwb_session *s,
		  void *user_data, bool timeout)
{
	(void)s;
	(void)user_data;
	if (g_tx.msg) {
		aliro_uwb_session_message_free(g_tx.msg);
	}
	g_tx.msg = m;
	g_tx.timeout = timeout;
	g_tx.count++;
}

static struct {
	int count;
	enum aliro_uwb_session_event_type last_type;
	enum cherry_ccc_session_state last_state;
} g_ev;

static void ev_cb(struct aliro_uwb_session_event *e, void *user_data)
{
	(void)user_data;
	g_ev.count++;
	g_ev.last_type = e->type;
	if (e->type == ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_STATUS) {
		g_ev.last_state = e->data.status->session_state;
	}
	aliro_uwb_session_event_free(e);
}

/* --- message helpers ---------------------------------------------------- */

/* Start a message with generous capacity; payload length is fixed up later. */
static struct aliro_uwb_msg_builder mk(uint8_t proto, uint8_t id)
{
	struct aliro_uwb_msg_builder b;

	aliro_uwb_msg_builder_init(&b, 128u);
	aliro_uwb_msg_builder_header(&b, proto, id, 0u);
	return b;
}

/* Write the real payload length into the header (bytes 2..3, big-endian). */
static void fix_plen(struct aliro_uwb_message *m)
{
	uint16_t plen = (uint16_t)(m->len - ALIRO_HEADER_LENGTH);

	m->data[2] = (uint8_t)(plen >> 8);
	m->data[3] = (uint8_t)plen;
}

/* Append the seven attributes an M2 must carry to advance M1_SENT -> M3_SENT. */
static void add_m2_attrs(struct aliro_uwb_msg_builder *b)
{
	aliro_uwb_msg_builder_add_u16(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER, 0x0001u);
	aliro_uwb_msg_builder_add_u8(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO, 0x00u);
	aliro_uwb_msg_builder_add_u8(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK, 0x01u);
	aliro_uwb_msg_builder_add_u8(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER, 4u);
	aliro_uwb_msg_builder_add_u8(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK, 0x01u);
	aliro_uwb_msg_builder_add_u32(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK, 0x05u);
	aliro_uwb_msg_builder_add_u8(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
		0xFFu);
}

static void add_m4_attrs(struct aliro_uwb_msg_builder *b)
{
	aliro_uwb_msg_builder_add_u32(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x1000u);
	aliro_uwb_msg_builder_add_u64(b, ALIRO_UWB_RANGING_SERVICE_ATTR_UWB_TIME0,
				      0u);
	aliro_uwb_msg_builder_add_u32(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY, 0x11223344u);
	aliro_uwb_msg_builder_add_u8(
		b, ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX, 9u);
}

void test_aliro_session(void)
{
	const uint32_t SID = 0x11223344u;
	uint16_t pvs[1] = { 0x0100u };
	uint16_t cfgs[1] = { 0x0001u };
	uint8_t combos[1] = { 0x00u };
	uint8_t ursk[32];
	struct cherry_ccc_capabilities ccc;
	struct cherry_core_event_device_capabilities caps;
	struct aliro_uwb_adapter_reader_config cfg;
	struct aliro_uwb_msg_builder b;

	memset(&g_tx, 0, sizeof(g_tx));
	memset(&g_ev, 0, sizeof(g_ev));
	for (size_t i = 0; i < sizeof(ursk); i++) {
		ursk[i] = (uint8_t)(i + 1u);
	}

	memset(&ccc, 0, sizeof(ccc));
	ccc.protocol_versions.len = 1u;
	ccc.protocol_versions.items = pvs;
	ccc.uwb_configs.len = 1u;
	ccc.uwb_configs.items = cfgs;
	ccc.pulse_shape_combos.len = 1u;
	ccc.pulse_shape_combos.items = combos;
	ccc.minimum_ran_multiplier = 4u;
	ccc.slot_bitmask = 0x01u;
	ccc.channel_bitmask = 0x01u;
	ccc.hopping_config_bitmask = 0x0Au;
	ccc.sync_code_index_bitmask = 0x00000005u;
	memset(&caps, 0, sizeof(caps));
	caps.ccc_capabilities = &ccc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.min_ran_multiplier = 8u;
	cfg.preferred_hopping_configs.count = 1u;
	cfg.preferred_hopping_configs.configs[0] =
		ALIRO_HOPPING_CONFIG_CONTINUOUS_DEFAULT;
	cfg.r1_antennas[0] = 1u; /* exercise the antenna-set branches in init */
	cfg.r1_antennas[1] = 2u;
	cfg.r2_antennas[0] = 3u;
	cfg.r2_antennas[1] = 4u;

	struct cherry *cx = cherry_create("host", NULL, NULL);
	struct aliro_uwb_adapter *adapter =
		aliro_uwb_adapter_create_reader(cx, &caps, &cfg);
	T_OK("adapter", adapter != NULL);

	struct cherry_common_diag_cfg diag; /* exercise the diag branch in init */
	memset(&diag, 0, sizeof(diag));
	diag.aoa = true;
	aliro_uwb_adapter_set_diagnostics(adapter, diag);

	t_group("create rejects null args");
	T_OK("create.null.ctx",
	     aliro_uwb_session_create(NULL, SID, ev_cb, tx_cb, NULL) == NULL);
	T_OK("create.null.cb",
	     aliro_uwb_session_create(adapter, SID, NULL, tx_cb, NULL) == NULL);
	T_OK("create.null.tx",
	     aliro_uwb_session_create(adapter, SID, ev_cb, NULL, NULL) == NULL);

	struct aliro_uwb_session *s =
		aliro_uwb_session_create(adapter, SID, ev_cb, tx_cb, NULL);
	T_OK("create.ok", s != NULL);
	T_EQ("create.state", s->state, CREATED);

	t_group("setters: null guard + happy");
	T_EQ("ursk.null", aliro_uwb_session_set_ursk(NULL, ursk),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("ursk.null2", aliro_uwb_session_set_ursk(s, NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("ursk.ok", aliro_uwb_session_set_ursk(s, ursk), ALIRO_UWB_ERR_NONE);
	T_EQ("pv.null", aliro_uwb_session_set_protocol_version(NULL, 0x0100u),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("pv.ok", aliro_uwb_session_set_protocol_version(s, 0x0100u),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("to.null", aliro_uwb_session_set_time_offset(NULL, 0),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("to.ok", aliro_uwb_session_set_time_offset(s, 0), ALIRO_UWB_ERR_NONE);

	t_group("suspend/resume/forced reject with no ccc session");
	T_EQ("suspend.null", aliro_uwb_session_suspend(NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("suspend.noccc", aliro_uwb_session_suspend(s),
	     ALIRO_UWB_ERR_INVALID_STATE);
	T_EQ("resume.null", aliro_uwb_session_resume(NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("resume.noccc", aliro_uwb_session_resume(s),
	     ALIRO_UWB_ERR_INVALID_STATE);
	T_EQ("fsuspend.null", aliro_uwb_session_forced_suspend(NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("fsuspend.noccc", aliro_uwb_session_forced_suspend(s),
	     ALIRO_UWB_ERR_INVALID_STATE);

	t_group("init_setup builds + transmits M1 (CREATED -> M1_SENT)");
	T_EQ("init.null", aliro_uwb_session_init_setup(NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("init.ok", aliro_uwb_session_init_setup(s), ALIRO_UWB_ERR_NONE);
	T_EQ("init.state", s->state, M1_SENT);
	T_OK("m1.captured", g_tx.msg != NULL);
	T_EQ("m1.proto", aliro_uwb_msg_protocol_header(g_tx.msg->data),
	     ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE);
	T_EQ("m1.id", aliro_uwb_msg_message_id(g_tx.msg->data),
	     ALIRO_UWB_MESSAGE_SETUP_M1);
	T_EQ("m1.timeout", g_tx.timeout, 1);
	T_EQ("init.badstate", aliro_uwb_session_init_setup(s),
	     ALIRO_UWB_ERR_INVALID_STATE);

	t_group("message_handle guards + framing");
	T_EQ("mh.null.s", aliro_uwb_session_message_handle(NULL, g_tx.msg),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("mh.null.m", aliro_uwb_session_message_handle(s, NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	struct aliro_uwb_message *shortm = malloc(sizeof(*shortm) + 3u);
	shortm->len = 3u;
	T_EQ("mh.short", aliro_uwb_session_message_handle(s, shortm),
	     ALIRO_UWB_ERR_MSG_MALFORMED);
	free(shortm);
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	b.message->data[2] = 0x00u; /* claim payload 10, actual 0 -> mismatch */
	b.message->data[3] = 0x0Au;
	T_EQ("mh.plen", aliro_uwb_session_message_handle(s, b.message),
	     ALIRO_UWB_ERR_MSG_MALFORMED);
	aliro_uwb_msg_free(b.message);
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_AP, 0u);
	fix_plen(b.message);
	T_EQ("mh.unsupproto", aliro_uwb_session_message_handle(s, b.message),
	     ALIRO_UWB_ERR_MESSAGE_UNSUPPORTED);
	aliro_uwb_msg_free(b.message);

	t_group("M2 advances to M3_SENT and transmits M3");
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	fix_plen(b.message);
	T_EQ("m2.ok", aliro_uwb_session_message_handle(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("m2.state", s->state, M3_SENT);
	T_EQ("m3.id", aliro_uwb_msg_message_id(g_tx.msg->data),
	     ALIRO_UWB_MESSAGE_SETUP_M3);
	aliro_uwb_msg_free(b.message);

	t_group("M4 brings the session up (M3_SENT -> RANGING, ccc active)");
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M4);
	add_m4_attrs(&b);
	fix_plen(b.message);
	T_EQ("m4.ok", aliro_uwb_session_message_handle(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("m4.state", s->state, RANGING);
	T_OK("m4.ccc", s->ccc_session != NULL);
	T_EQ("m4.active", g_ev.last_state, CHERRY_CCC_SESSION_STATE_ACTIVE);
	aliro_uwb_msg_free(b.message);

	t_group("ccc SESSION_ERROR event -> peer general-error notification");
	/* The shim registers aliro_ccc_cb as the ccc session's callback (base.cb,
	 * the first member of the ccc session) and invokes it with cherry events.
	 * The host shim only ever emits STATUS events, so drive an ERROR event the
	 * same way the shim would and confirm aliro_ccc_cb answers with a
	 * general-error notification via the transmit callback. */
	cherry_ccc_cb_t ccc_cb = *(cherry_ccc_cb_t *)s->ccc_session;
	void *ccc_ud = cherry_ccc_session_get_user_data(s->ccc_session);
	struct cherry_ccc_event *ee = malloc(sizeof(*ee));
	ee->type = CHERRY_CCC_EVENT_TYPE_SESSION_ERROR;
	ee->session = s->ccc_session;
	ee->data.error = malloc(sizeof(*ee->data.error));
	ee->data.error->status_err = CHERRY_ERR_INTERNAL;
	ccc_cb(ee, ccc_ud);
	T_EQ("err.notify.proto", aliro_uwb_msg_protocol_header(g_tx.msg->data),
	     ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION);
	T_EQ("err.notify.id", aliro_uwb_msg_message_id(g_tx.msg->data),
	     ALIRO_UWB_MESSAGE_NOTIFICATION_EVENT);
	T_EQ("err.state", s->state, RANGING); /* error does not change state */

	t_group("graceful suspend (RANGING -> SUSPEND_REQ_SENT)");
	T_EQ("suspend.ok", aliro_uwb_session_suspend(s), ALIRO_UWB_ERR_NONE);
	T_EQ("suspend.state", s->state, SUSPEND_REQ_SENT);
	T_EQ("suspend.reqid", aliro_uwb_msg_message_id(g_tx.msg->data),
	     ALIRO_UWB_MESSAGE_SUSPEND_REQUEST);

	t_group("forced_suspend stops immediately");
	s->state = RANGING;
	T_EQ("fsuspend.ok", aliro_uwb_session_forced_suspend(s),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("fsuspend.state", s->state, SUSPENDED);

	t_group("resume (SUSPENDED -> RESUME_REQ_SENT)");
	T_EQ("resume.ok", aliro_uwb_session_resume(s), ALIRO_UWB_ERR_NONE);
	T_EQ("resume.state", s->state, RESUME_REQ_SENT);
	T_EQ("resume.reqid", aliro_uwb_msg_message_id(g_tx.msg->data),
	     ALIRO_UWB_MESSAGE_RESUME_REQUEST);

	t_group("suspend/resume reject a wrong state while ccc is live");
	s->state = RESUME_REQ_SENT; /* not RANGING */
	T_EQ("suspend.badstate", aliro_uwb_session_suspend(s),
	     ALIRO_UWB_ERR_INVALID_STATE);
	s->state = RANGING; /* not SUSPENDED */
	T_EQ("resume.badstate", aliro_uwb_session_resume(s),
	     ALIRO_UWB_ERR_INVALID_STATE);

	t_group("internal init/start/stop");
	T_EQ("init.null", aliro_uwb_session_init(NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("start.null", aliro_uwb_session_start(NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("stop.null", aliro_uwb_session_stop(NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("start.ok", aliro_uwb_session_start(s), ALIRO_UWB_ERR_NONE);
	T_EQ("stop.ok", aliro_uwb_session_stop(s), ALIRO_UWB_ERR_NONE);
	T_EQ("stop.state", s->state, SUSPENDED);

	t_group("start/stop take the close-on-error branch (no ccc session)");
	/* A session with no ccc_session: the cherry call reports an error, so
	 * start/stop run session_close (which frees the session here). */
	struct aliro_uwb_session *sf1 =
		aliro_uwb_session_create(adapter, 10u, ev_cb, tx_cb, NULL);
	T_EQ("start.close", aliro_uwb_session_start(sf1),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	struct aliro_uwb_session *sf2 =
		aliro_uwb_session_create(adapter, 11u, ev_cb, tx_cb, NULL);
	T_EQ("stop.close", aliro_uwb_session_stop(sf2),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);

	t_group("notification dispatch through message_handle");
	s->state = RANGING;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING);
	aliro_uwb_msg_builder_add_u8(
		&b, ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_RANGING_SUSPENDED,
		0u);
	fix_plen(b.message);
	T_EQ("notif.ok", aliro_uwb_session_message_handle(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("notif.state", s->state, SUSPENDED);
	aliro_uwb_msg_free(b.message);

	t_group("free helpers tolerate their inputs");
	aliro_uwb_session_event_free(NULL);
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE, 0u);
	fix_plen(b.message);
	aliro_uwb_session_message_free(b.message);

	t_group("destroy with a live ccc session, then null + no-ccc paths");
	if (g_tx.msg) {
		aliro_uwb_session_message_free(g_tx.msg);
		g_tx.msg = NULL;
	}
	aliro_uwb_session_destroy(s);
	aliro_uwb_session_destroy(NULL);
	struct aliro_uwb_session *s2 =
		aliro_uwb_session_create(adapter, 2u, ev_cb, tx_cb, NULL);
	aliro_uwb_session_destroy(s2); /* no ccc_session -> frees directly */

	t_group("session_init fail path (M4 without a URSK)");
	/* No set_ursk: the ccc session is created but its start fails with
	 * SESSION_CONFIG, so aliro_uwb_session_init takes goto-fail and frees. */
	struct aliro_uwb_session *s3 =
		aliro_uwb_session_create(adapter, 3u, ev_cb, tx_cb, NULL);
	T_EQ("s3.init", aliro_uwb_session_init_setup(s3), ALIRO_UWB_ERR_NONE);
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	fix_plen(b.message);
	T_EQ("s3.m2", aliro_uwb_session_message_handle(s3, b.message),
	     ALIRO_UWB_ERR_NONE);
	aliro_uwb_msg_free(b.message);
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M4);
	add_m4_attrs(&b);
	fix_plen(b.message);
	/* init fails -> handle_m4 returns the mapped cherry error; s3 self-frees. */
	T_EQ("s3.m4.fail", aliro_uwb_session_message_handle(s3, b.message),
	     ALIRO_UWB_ERR_SESSION_CONFIG);
	aliro_uwb_msg_free(b.message);
	if (g_tx.msg) {
		aliro_uwb_session_message_free(g_tx.msg);
		g_tx.msg = NULL;
	}

	aliro_uwb_adapter_destroy(adapter);
	cherry_destroy_sync(cx);
}
