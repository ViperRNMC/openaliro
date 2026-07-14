/** @file test_aliro_builder.c — big-endian TLV message builder. */
#include <stdlib.h>
#include <string.h>

#include "aliro_uwb_msg_builder.h"
#include "aliro_uwb_msg_parser.h"
#include "test.h"

void test_aliro_builder(void)
{
	struct aliro_uwb_msg_builder b;
	uint8_t raw[3] = { 0xaa, 0xbb, 0xcc };
	uint16_t arr[2] = { 0x1111, 0x2222 };

	t_group("init + append every attribute type");
	T_OK("init", aliro_uwb_msg_builder_init(&b, 64));
	aliro_uwb_msg_builder_header(&b, ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
				     ALIRO_UWB_MESSAGE_SETUP_M1, 34u);
	T_OK("add_u8", aliro_uwb_msg_builder_add_u8(&b, 0x01u, 0x42u));
	T_OK("add_u16", aliro_uwb_msg_builder_add_u16(&b, 0x02u, 0xbeefu));
	T_OK("add_u32", aliro_uwb_msg_builder_add_u32(&b, 0x03u, 0xdeadbeefu));
	T_OK("add_u64", aliro_uwb_msg_builder_add_u64(&b, 0x04u, 0x1122334455667788ull));
	T_OK("add_bytes", aliro_uwb_msg_builder_add_bytes(&b, 0x05u, 3u, raw));
	T_OK("add_u16arr", aliro_uwb_msg_builder_add_u16_array(&b, 0x06u, 2u, arr));

	t_group("header bytes");
	T_EQ("hdr.proto", b.message->data[0], ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE);
	T_EQ("hdr.id", b.message->data[1], ALIRO_UWB_MESSAGE_SETUP_M1);
	T_EQ("hdr.plen", (b.message->data[2] << 8) | b.message->data[3], 34);

	t_group("round-trip through the parser");
	struct aliro_uwb_msg_parser p = ALIRO_UWB_MSG_PARSER_INIT(b.message);
	struct aliro_uwb_msg_attribute *a;
	uint8_t v8 = 0;
	uint16_t v16 = 0;
	uint32_t v32 = 0;
	uint64_t v64 = 0;

	a = aliro_uwb_msg_next_attribute(&p);
	T_OK("rt.u8", a && aliro_uwb_msg_read_u8(a, "u8", &v8) && v8 == 0x42u);
	a = aliro_uwb_msg_next_attribute(&p);
	T_OK("rt.u16", a && aliro_uwb_msg_read_u16(a, "u16", &v16) && v16 == 0xbeefu);
	a = aliro_uwb_msg_next_attribute(&p);
	T_OK("rt.u32", a && aliro_uwb_msg_read_u32(a, "u32", &v32) && v32 == 0xdeadbeefu);
	a = aliro_uwb_msg_next_attribute(&p);
	T_OK("rt.u64", a && aliro_uwb_msg_read_u64(a, "u64", &v64) &&
			     v64 == 0x1122334455667788ull);
	a = aliro_uwb_msg_next_attribute(&p);
	T_OK("rt.bytes", a && a->id == 0x05u && a->length == 3u &&
			     memcmp(a->value, raw, 3) == 0);
	a = aliro_uwb_msg_next_attribute(&p);
	T_OK("rt.arr", a && a->id == 0x06u && a->length == 4u);
	T_OK("rt.end", aliro_uwb_msg_next_attribute(&p) == NULL);
	free(b.message);

	t_group("capacity overflow refused");
	struct aliro_uwb_msg_builder small;
	T_OK("init.small", aliro_uwb_msg_builder_init(&small, 2u)); /* cap = 4+2 = 6 */
	aliro_uwb_msg_builder_header(&small, 1u, 0u, 0u);          /* len = 4 */
	T_OK("overflow", !aliro_uwb_msg_builder_add_u32(&small, 0x03u, 0x11223344u));
	free(small.message);

	t_group("u16_array guards");
	struct aliro_uwb_msg_builder b2;
	aliro_uwb_msg_builder_init(&b2, 16u);
	aliro_uwb_msg_builder_header(&b2, 1u, 0u, 0u);
	T_OK("arr.count0", !aliro_uwb_msg_builder_add_u16_array(&b2, 0x06u, 0u, arr));
	T_OK("arr.null", !aliro_uwb_msg_builder_add_u16_array(&b2, 0x06u, 2u, NULL));
	free(b2.message);
}
