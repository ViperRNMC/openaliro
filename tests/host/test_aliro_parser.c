/** @file test_aliro_parser.c — TLV attribute iteration + big-endian readers. */
#include "aliro_uwb_msg_parser.h"
#include "aliro_uwb_msg_spec.h"
#include "test.h"

void test_aliro_parser(void)
{
	/* 4-byte header, then attr(id=0x0A,len=1,0x42) and attr(id=0x0B,len=2,0xBEEF). */
	static const uint8_t buf[] = {
		0x01, 0x00, 0x00, 0x06,
		0x0a, 0x01, 0x42,
		0x0b, 0x02, 0xbe, 0xef,
	};
	struct aliro_uwb_msg_parser p = {
		.length = sizeof(buf), .offset = ALIRO_HEADER_LENGTH, .data = buf,
	};
	struct aliro_uwb_msg_attribute *a;
	uint8_t v8 = 0;
	uint16_t v16 = 0;

	t_group("iterate + read");
	a = aliro_uwb_msg_next_attribute(&p);
	T_OK("a1.id", a && a->id == 0x0au && a->length == 1u);
	T_OK("a1.read_u8", aliro_uwb_msg_read_u8(a, "u8", &v8) && v8 == 0x42u);
	/* Wrong-width read is rejected. */
	T_OK("a1.read_u16_mismatch", !aliro_uwb_msg_read_u16(a, "u16", &v16));

	a = aliro_uwb_msg_next_attribute(&p);
	T_OK("a2.id", a && a->id == 0x0bu && a->length == 2u);
	T_OK("a2.read_u16", aliro_uwb_msg_read_u16(a, "u16", &v16) && v16 == 0xbeefu);
	T_OK("a2.read_u8_mismatch", !aliro_uwb_msg_read_u8(a, "u8", &v8));

	T_OK("end", aliro_uwb_msg_next_attribute(&p) == NULL);

	t_group("u32/u64 readers + width guards");
	static const uint8_t wide[] = {
		0x01, 0x00, 0x00, 0x0c,
		0x0c, 0x04, 0x11, 0x22, 0x33, 0x44,             /* u32 */
		0x0d, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, /* u64 */
	};
	struct aliro_uwb_msg_parser wp = {
		.length = sizeof(wide), .offset = ALIRO_HEADER_LENGTH, .data = wide,
	};
	uint32_t v32 = 0;
	uint64_t v64 = 0;
	a = aliro_uwb_msg_next_attribute(&wp);
	T_OK("u32.read", aliro_uwb_msg_read_u32(a, "u32", &v32) && v32 == 0x11223344u);
	T_OK("u32.as64_bad", !aliro_uwb_msg_read_u64(a, "u64", &v64));
	a = aliro_uwb_msg_next_attribute(&wp);
	T_OK("u64.read", aliro_uwb_msg_read_u64(a, "u64", &v64) &&
			     v64 == 0x0102030405060708ull);

	t_group("overrun + empty payload");
	/* Declared length 8 with only 2 payload bytes: next_attribute returns NULL. */
	static const uint8_t ov[] = { 0x01, 0x00, 0x00, 0x04, 0x0c, 0x08, 0x01, 0x02 };
	struct aliro_uwb_msg_parser op = {
		.length = sizeof(ov), .offset = ALIRO_HEADER_LENGTH, .data = ov,
	};
	T_OK("overrun", aliro_uwb_msg_next_attribute(&op) == NULL);
	/* offset already at end -> immediate NULL. */
	struct aliro_uwb_msg_parser ep = { .length = 4, .offset = 4, .data = ov };
	T_OK("empty", aliro_uwb_msg_next_attribute(&ep) == NULL);

	t_group("lone trailing byte (no over-read)");
	/* One good attribute then a single header-less trailing byte. The cursor
	 * must stop cleanly: reading attr->length for that lone byte would over-read
	 * one byte past the payload. Regression found by the fuzz harness. */
	static const uint8_t tail[] = { 0x01, 0x00, 0x00, 0x04, 0x0a, 0x01, 0x42, 0x0b };
	struct aliro_uwb_msg_parser tp = {
		.length = sizeof(tail), .offset = ALIRO_HEADER_LENGTH, .data = tail,
	};
	a = aliro_uwb_msg_next_attribute(&tp);
	T_OK("tail.a1", a && a->id == 0x0au && a->length == 1u);
	T_OK("tail.stop", aliro_uwb_msg_next_attribute(&tp) == NULL);
}
