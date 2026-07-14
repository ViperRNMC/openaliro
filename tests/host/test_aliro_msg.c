/** @file test_aliro_msg.c — M1-M4 builders + ranging/notification dispatch. */
#include <stdlib.h>
#include <string.h>

#include "aliro_uwb_internal.h"
#include "aliro_uwb_msg.h"
#include "aliro_uwb_msg_builder.h"
#include "aliro_uwb_msg_parser.h"
#include "aliro_uwb_msg_spec.h"
#include "test.h"

/* --- transmit + event capture ------------------------------------------- */

static struct {
	struct aliro_uwb_message *msg;
	int count;
} g_tx;

static void tx_cb(struct aliro_uwb_message *m, struct aliro_uwb_session *s,
		  void *user_data, bool timeout)
{
	(void)s;
	(void)user_data;
	(void)timeout;
	if (g_tx.msg) {
		aliro_uwb_msg_free(g_tx.msg);
	}
	g_tx.msg = m;
	g_tx.count++;
}

static void ev_cb(struct aliro_uwb_session_event *e, void *user_data)
{
	(void)user_data;
	aliro_uwb_session_event_free(e);
}

/* --- message helpers ---------------------------------------------------- */

static struct aliro_uwb_msg_builder mk(uint8_t proto, uint8_t id)
{
	struct aliro_uwb_msg_builder b;

	aliro_uwb_msg_builder_init(&b, 128u);
	aliro_uwb_msg_builder_header(&b, proto, id, 0u);
	return b;
}

static void fix_plen(struct aliro_uwb_message *m)
{
	uint16_t plen = (uint16_t)(m->len - ALIRO_HEADER_LENGTH);

	m->data[2] = (uint8_t)(plen >> 8);
	m->data[3] = (uint8_t)plen;
}

/* Append an attribute of a deliberately wrong width to force a read failure. */
static void add_wrong(struct aliro_uwb_msg_builder *b, uint8_t id, int width)
{
	if (width == 1) {
		aliro_uwb_msg_builder_add_u8(b, id, 0u);
	} else {
		aliro_uwb_msg_builder_add_u16(b, id, 0u);
	}
}

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

/* Read the u32 value of the first attribute with @id, or fail_val if absent. */
static uint32_t find_u32(struct aliro_uwb_message *m, uint8_t id, uint32_t fail_val)
{
	struct aliro_uwb_msg_parser p = ALIRO_UWB_MSG_PARSER_INIT(m);
	struct aliro_uwb_msg_attribute *a;
	uint32_t v;

	while ((a = aliro_uwb_msg_next_attribute(&p))) {
		if (a->id == id && aliro_uwb_msg_read_u32(a, "x", &v)) {
			return v;
		}
	}
	return fail_val;
}

void test_aliro_msg(void)
{
	const uint32_t SID = 0x11223344u;
	uint16_t pvs[1] = { 0x0100u };
	uint16_t cfgs[1] = { 0x0001u };
	uint8_t combos[1] = { 0x00u };
	uint8_t oui[3] = { 0xa1, 0xb2, 0xc3 };
	uint8_t ursk[32];
	struct cherry_ccc_capabilities ccc;
	struct cherry_core_event_device_capabilities caps;
	struct aliro_uwb_adapter_reader_config cfg;
	struct aliro_uwb_msg_builder b;
	struct aliro_uwb_message *m;

	memset(&g_tx, 0, sizeof(g_tx));
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

	struct cherry *cx = cherry_create("host", NULL, NULL);
	struct aliro_uwb_adapter *adapter =
		aliro_uwb_adapter_create_reader(cx, &caps, &cfg);
	struct aliro_uwb_session *s =
		aliro_uwb_session_create(adapter, SID, ev_cb, tx_cb, NULL);
	T_OK("setup", adapter != NULL && s != NULL);
	aliro_uwb_session_set_ursk(s, ursk);
	aliro_uwb_session_set_protocol_version(s, 0x0100u);

	t_group("build M1 (header + attributes round-trip)");
	m = aliro_uwb_msg_build_m1(s);
	T_OK("m1", m != NULL);
	T_EQ("m1.proto", aliro_uwb_msg_protocol_header(m->data),
	     ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE);
	T_EQ("m1.id", aliro_uwb_msg_message_id(m->data), ALIRO_UWB_MESSAGE_SETUP_M1);
	T_EQ("m1.plen", aliro_uwb_msg_payload_length(m->data),
	     (uint16_t)(m->len - ALIRO_HEADER_LENGTH));
	T_EQ("m1.sid",
	     find_u32(m, ALIRO_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, 0u), SID);
	aliro_uwb_msg_free(m);

	t_group("build suspend/resume requests + general error");
	m = aliro_uwb_msg_build_suspend_resume_request(s, true);
	T_EQ("suspreq.id", aliro_uwb_msg_message_id(m->data),
	     ALIRO_UWB_MESSAGE_SUSPEND_REQUEST);
	T_EQ("suspreq.sid",
	     find_u32(m, ALIRO_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, 0u), SID);
	aliro_uwb_msg_free(m);
	m = aliro_uwb_msg_build_suspend_resume_request(s, false);
	T_EQ("resreq.id", aliro_uwb_msg_message_id(m->data),
	     ALIRO_UWB_MESSAGE_RESUME_REQUEST);
	aliro_uwb_msg_free(m);
	m = aliro_uwb_msg_build_general_error(
		s, ALIRO_UWB_NOTIFICATION_GENERAL_ERROR_UNKNOWN);
	T_EQ("gerr.proto", aliro_uwb_msg_protocol_header(m->data),
	     ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION);
	T_EQ("gerr.id", aliro_uwb_msg_message_id(m->data),
	     ALIRO_UWB_MESSAGE_NOTIFICATION_EVENT);
	aliro_uwb_msg_free(m);

	t_group("process_ranging guards + unsupported id");
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_RESUME_REQUEST);
	fix_plen(b.message);
	T_EQ("pr.null.s", aliro_uwb_msg_process_ranging(NULL, b.message),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("pr.null.m", aliro_uwb_msg_process_ranging(s, NULL),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	T_EQ("pr.unsup", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_MESSAGE_UNSUPPORTED);
	aliro_uwb_msg_free(b.message);

	t_group("full handshake M1 -> M2 -> M4 (session goes RANGING)");
	T_EQ("init", aliro_uwb_session_init_setup(s), ALIRO_UWB_ERR_NONE);
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	fix_plen(b.message);
	T_EQ("m2", aliro_uwb_msg_process_ranging(s, b.message), ALIRO_UWB_ERR_NONE);
	T_EQ("m2.state", s->state, M3_SENT);
	T_EQ("m3.id", aliro_uwb_msg_message_id(g_tx.msg->data),
	     ALIRO_UWB_MESSAGE_SETUP_M3);
	aliro_uwb_msg_free(b.message);
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M4);
	add_m4_attrs(&b);
	fix_plen(b.message);
	T_EQ("m4", aliro_uwb_msg_process_ranging(s, b.message), ALIRO_UWB_ERR_NONE);
	T_EQ("m4.state", s->state, RANGING);
	T_OK("m4.ccc", s->ccc_session != NULL);
	aliro_uwb_msg_free(b.message);

	t_group("M2 vendor variant (mask + VENDOR_SPECIFIC attribute)");
	s->state = M1_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	aliro_uwb_msg_builder_add_bytes(
		&b, ALIRO_UWB_RANGING_SERVICE_ATTR_VENDOR_SPECIFIC, 3u, oui);
	fix_plen(b.message);
	T_EQ("m2.vendor", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("m2v.state", s->state, M3_SENT);
	aliro_uwb_msg_free(b.message);

	t_group("ranging error paths");
	s->state = M1_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	aliro_uwb_msg_builder_add_u16(
		&b, ALIRO_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER, 1u);
	fix_plen(b.message);
	T_EQ("m2.missing", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_MSG_MALFORMED);
	aliro_uwb_msg_free(b.message);

	s->state = M1_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	aliro_uwb_msg_builder_add_u8(
		&b, ALIRO_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK, 0x04u);
	fix_plen(b.message);
	T_EQ("m2.badchan", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_INVALID_PARAMETER);
	aliro_uwb_msg_free(b.message);

	s->state = M1_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	aliro_uwb_msg_builder_add_u8(
		&b, ALIRO_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
		0x00u);
	fix_plen(b.message);
	T_EQ("m2.nohop", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_MSG_MALFORMED);
	aliro_uwb_msg_free(b.message);

	s->state = RANGING; /* wrong state for M2 */
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	add_m2_attrs(&b);
	fix_plen(b.message);
	T_EQ("m2.badstate", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_INVALID_STATE);
	aliro_uwb_msg_free(b.message);

	s->state = M1_SENT; /* wrong state for M4 */
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M4);
	add_m4_attrs(&b);
	fix_plen(b.message);
	T_EQ("m4.badstate", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_INVALID_STATE);
	aliro_uwb_msg_free(b.message);

	s->state = M3_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M4);
	aliro_uwb_msg_builder_add_u32(
		&b, ALIRO_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x1000u);
	fix_plen(b.message);
	T_EQ("m4.missing", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_MSG_MALFORMED);
	aliro_uwb_msg_free(b.message);

	t_group("every attribute parser rejects a wrong-width value");
	{
		struct {
			uint8_t id;
			int width;
		} bad[] = {
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER, 1 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO, 2 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK, 2 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER, 2 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK, 2 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK, 1 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX, 2 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK, 2 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 1 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_UWB_TIME0, 1 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY, 1 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_STATUS, 2 },
			{ ALIRO_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, 1 },
		};

		for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
			s->state = M1_SENT;
			b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
			       ALIRO_UWB_MESSAGE_SETUP_M2);
			add_wrong(&b, bad[i].id, bad[i].width);
			fix_plen(b.message);
			T_EQ("attr.badwidth",
			     aliro_uwb_msg_process_ranging(s, b.message),
			     ALIRO_UWB_ERR_MSG_MALFORMED);
			aliro_uwb_msg_free(b.message);
		}
	}

	t_group("unknown attribute id is ignored (parse continues)");
	s->state = M1_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SETUP_M2);
	aliro_uwb_msg_builder_add_u8(
		&b, ALIRO_UWB_RANGING_SERVICE_ATTR_NUMBER_CHAPS_PER_SLOT, 0u);
	fix_plen(b.message);
	/* Parsed (default case), then fails the M2 attribute-mask check. */
	T_EQ("m2.unknownattr", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_MSG_MALFORMED);
	aliro_uwb_msg_free(b.message);

	t_group("suspend request handler (RANGING) transmits a response");
	s->state = RANGING;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SUSPEND_REQUEST);
	aliro_uwb_msg_builder_add_u32(
		&b, ALIRO_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER, SID);
	fix_plen(b.message);
	T_EQ("susp.req", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("susp.resp.id", aliro_uwb_msg_message_id(g_tx.msg->data),
	     ALIRO_UWB_MESSAGE_SUSPEND_RESPONSE);
	aliro_uwb_msg_free(b.message);

	t_group("suspend response reject keeps ranging");
	s->state = SUSPEND_REQ_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SUSPEND_RESPONSE);
	aliro_uwb_msg_builder_add_u8(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_STATUS,
				     ALIRO_UWB_RANGING_SERVICE_STATUS_REJECT);
	fix_plen(b.message);
	T_EQ("susp.rej", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("susp.rej.state", s->state, RANGING);
	aliro_uwb_msg_free(b.message);

	t_group("suspend response accept stops");
	s->state = SUSPEND_REQ_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_SUSPEND_RESPONSE);
	aliro_uwb_msg_builder_add_u8(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_STATUS,
				     ALIRO_UWB_RANGING_SERVICE_STATUS_ACCEPT);
	fix_plen(b.message);
	T_EQ("susp.acc", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("susp.acc.state", s->state, SUSPENDED);
	aliro_uwb_msg_free(b.message);

	t_group("resume response restarts ranging (time offset applied)");
	aliro_uwb_session_set_time_offset(s, 1000);
	s->state = RESUME_REQ_SENT;
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
	       ALIRO_UWB_MESSAGE_RESUME_RESPONSE);
	aliro_uwb_msg_builder_add_u32(
		&b, ALIRO_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, 0x2000u);
	aliro_uwb_msg_builder_add_u64(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_UWB_TIME0,
				      0u);
	fix_plen(b.message);
	T_EQ("resume.resp", aliro_uwb_msg_process_ranging(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	T_EQ("resume.resp.state", s->state, RANGING);
	aliro_uwb_msg_free(b.message);

	t_group("event notification (busy / general error / descriptor / unknown)");
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ALIRO_UWB_MESSAGE_NOTIFICATION_EVENT);
	aliro_uwb_msg_builder_add_bytes(
		&b, ALIRO_UWB_MESSAGE_NOTIFICATION_EVENT_ATTR_BUSY, 0u, NULL);
	aliro_uwb_msg_builder_add_u8(
		&b, ALIRO_UWB_MESSAGE_NOTIFICATION_EVENT_ATTR_GENERAL_ERROR, 2u);
	aliro_uwb_msg_builder_add_bytes(
		&b, ALIRO_UWB_MESSAGE_NOTIFICATION_EVENT_ATTR_READER_DESCRIPTOR, 0u,
		NULL);
	aliro_uwb_msg_builder_add_bytes(&b, 0x7Fu, 0u, NULL);
	fix_plen(b.message);
	T_EQ("notif.event", aliro_uwb_msg_process_notification(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	aliro_uwb_msg_free(b.message);

	b = mk(ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ALIRO_UWB_MESSAGE_NOTIFICATION_EVENT);
	aliro_uwb_msg_builder_add_u16(
		&b, ALIRO_UWB_MESSAGE_NOTIFICATION_EVENT_ATTR_GENERAL_ERROR, 0u);
	fix_plen(b.message);
	T_EQ("notif.event.bad",
	     aliro_uwb_msg_process_notification(s, b.message),
	     ALIRO_UWB_ERR_MSG_MALFORMED);
	aliro_uwb_msg_free(b.message);

	t_group("ranging notification drives the state machine");
	struct {
		uint8_t attr;
		enum aliro_uwb_session_state pre;
		enum aliro_uwb_session_state post;
	} rn[] = {
		{ ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING, CREATED,
		  M1_SENT },
		{ ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_SETUP_LATER,
		  M1_SENT, CREATED },
		{ ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_RANGING_SUSPENDED,
		  RANGING, SUSPENDED },
		{ ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_RESUME,
		  SUSPENDED, RESUME_REQ_SENT },
		{ ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_RESUME_LATER,
		  RESUME_REQ_SENT, SUSPENDED },
	};
	for (size_t i = 0; i < sizeof(rn) / sizeof(rn[0]); i++) {
		s->state = rn[i].pre;
		b = mk(ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION,
		       ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING);
		aliro_uwb_msg_builder_add_u8(&b, rn[i].attr, 0u);
		fix_plen(b.message);
		T_EQ("rn.ok", aliro_uwb_msg_process_notification(s, b.message),
		     ALIRO_UWB_ERR_NONE);
		T_EQ("rn.state", s->state, rn[i].post);
		aliro_uwb_msg_free(b.message);
	}

	t_group("ranging notification: no-op attr + bad-state error");
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING);
	aliro_uwb_msg_builder_add_u8(
		&b, ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_SECURE_RANGING_FAILED,
		0u);
	fix_plen(b.message);
	T_EQ("rn.secfail", aliro_uwb_msg_process_notification(s, b.message),
	     ALIRO_UWB_ERR_NONE);
	aliro_uwb_msg_free(b.message);

	s->state = RANGING; /* wrong state for setup-later */
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING);
	aliro_uwb_msg_builder_add_u8(
		&b, ALIRO_UWB_MESSAGE_NOTIFICATION_RANGING_ATTR_INIT_RANGING_SETUP_LATER,
		0u);
	fix_plen(b.message);
	T_EQ("rn.badstate", aliro_uwb_msg_process_notification(s, b.message),
	     ALIRO_UWB_ERR_INVALID_STATE);
	aliro_uwb_msg_free(b.message);

	t_group("informational + unknown notification ids");
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION,
	       ALIRO_UWB_MESSAGE_NOTIFICATION_READER_STATUS_CHANGED);
	fix_plen(b.message);
	T_EQ("notif.readerstatus",
	     aliro_uwb_msg_process_notification(s, b.message), ALIRO_UWB_ERR_NONE);
	aliro_uwb_msg_free(b.message);
	b = mk(ALIRO_UWB_PROTOCOL_TYPE_NOTIFICATION, 0x7Fu);
	fix_plen(b.message);
	T_EQ("notif.unknownid",
	     aliro_uwb_msg_process_notification(s, b.message), ALIRO_UWB_ERR_NONE);
	aliro_uwb_msg_free(b.message);

	if (g_tx.msg) {
		aliro_uwb_msg_free(g_tx.msg);
		g_tx.msg = NULL;
	}
	s->state = RANGING;
	aliro_uwb_session_destroy(s);
	aliro_uwb_adapter_destroy(adapter);
	cherry_destroy_sync(cx);
}
