/*
 * Host unit test for the engine's pure port headers
 * (modules/woz_uwb/src/facade/{woz_bytes,woz_util}.h).
 *
 * These two carry no OS dependency at all, so they compile and run on the host
 * with no backend selected. They back the shared engine's endian load/store and
 * container/compare macros, so a silent edit here would corrupt the codec
 * on-silicon with no build error. This pins their behavior. The platform-bound
 * headers (woz_port.h, woz_log.h) only prove out on target; see verify_port.sh
 * for the on-target build/link guard.
 *
 * Plain C, no toolchain. Returns nonzero on any failure.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "woz_util.h"
#include "woz_bytes.h"

static int failures;

#define CHECK(name, cond)                                                    \
	do {                                                                 \
		if (cond) {                                                  \
			printf("  ok   %s\n", (name));                       \
		} else {                                                     \
			printf("  FAIL %s  (%s:%d)\n", (name), __FILE__,     \
			       __LINE__);                                    \
			failures++;                                          \
		}                                                            \
	} while (0)

static void test_byteorder(void)
{
	uint8_t buf[4];

	printf("byteorder (endian-neutral load/store)\n");

	/* Known vectors — independent of host endianness. */
	const uint8_t le32[4] = { 0x44, 0x33, 0x22, 0x11 };
	const uint8_t be32[4] = { 0x11, 0x22, 0x33, 0x44 };
	CHECK("get_le32", sys_get_le32(le32) == 0x11223344u);
	CHECK("get_be32", sys_get_be32(be32) == 0x11223344u);
	CHECK("get_le16", sys_get_le16(le32) == 0x3344u);
	CHECK("get_be16", sys_get_be16(be32) == 0x1122u);

	memset(buf, 0, sizeof(buf));
	sys_put_le32(0x11223344u, buf);
	CHECK("put_le32", memcmp(buf, le32, 4) == 0);
	sys_put_be32(0x11223344u, buf);
	CHECK("put_be32", memcmp(buf, be32, 4) == 0);

	memset(buf, 0, sizeof(buf));
	sys_put_le16(0xABCDu, buf);
	CHECK("put_le16", buf[0] == 0xCD && buf[1] == 0xAB);
	sys_put_be16(0xABCDu, buf);
	CHECK("put_be16", buf[0] == 0xAB && buf[1] == 0xCD);

	/* Round-trip: put then get is identity. */
	sys_put_le32(0xDEADBEEFu, buf);
	CHECK("roundtrip_le32", sys_get_le32(buf) == 0xDEADBEEFu);
	sys_put_be32(0xDEADBEEFu, buf);
	CHECK("roundtrip_be32", sys_get_be32(buf) == 0xDEADBEEFu);
}

/* Scratch record the compat-shim tests write through to prove the container_of
 * and byte-order helpers address the right member. */
struct co_probe {
	uint32_t pad;
	uint16_t field;
};

/* IS_ENABLED contract: 1 when defined to 1, 0 when undefined. */
#define CFG_ON 1

static void test_util(void)
{
	const uint8_t arr[7] = { 0 };
	struct co_probe probe;
	uint16_t *fp = &probe.field;

	printf("util (container/compare macros + IS_ENABLED)\n");

	CHECK("ARRAY_SIZE", ARRAY_SIZE(arr) == 7);
	CHECK("MIN", MIN(3, 5) == 3);
	CHECK("MAX", MAX(3, 5) == 5);
	CHECK("CLAMP_hi", CLAMP(10, 0, 5) == 5);
	CHECK("CLAMP_lo", CLAMP(-1, 0, 5) == 0);
	CHECK("CLAMP_in", CLAMP(3, 0, 5) == 3);
	CHECK("BIT_0", BIT(0) == 1u);
	CHECK("BIT_4", BIT(4) == 16u);
	CHECK("DIV_ROUND_UP_up", DIV_ROUND_UP(10, 3) == 4);
	CHECK("DIV_ROUND_UP_exact", DIV_ROUND_UP(9, 3) == 3);
	CHECK("ROUND_UP_up", ROUND_UP(10, 4) == 12);
	CHECK("ROUND_UP_exact", ROUND_UP(8, 4) == 8);
	CHECK("CONTAINER_OF",
	      CONTAINER_OF(fp, struct co_probe, field) == &probe);

	CHECK("IS_ENABLED_defined", IS_ENABLED(CFG_ON) == 1);
	CHECK("IS_ENABLED_undefined", IS_ENABLED(CFG_OFF) == 0);
}

int main(void)
{
	test_byteorder();
	test_util();

	printf("\nport headers: %s (%d failure%s)\n",
	       failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
