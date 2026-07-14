/** @file test.c — shared host-test harness implementation. */
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int t_fail;
int t_pending;
int t_pass;

void t_hex(char *dst, const uint8_t *b, size_t n)
{
	static const char H[] = "0123456789abcdef";

	for (size_t i = 0; i < n; i++) {
		dst[2 * i] = H[b[i] >> 4];
		dst[2 * i + 1] = H[b[i] & 0xf];
	}
	dst[2 * n] = '\0';
}

int t_unhex(uint8_t *dst, const char *hex, size_t cap)
{
	size_t n = strlen(hex) / 2;

	if (n > cap) {
		return -1;
	}
	for (size_t i = 0; i < n; i++) {
		unsigned int byte;

		if (sscanf(&hex[2 * i], "%2x", &byte) != 1) {
			return -1;
		}
		dst[i] = (uint8_t)byte;
	}
	return (int)n;
}

void t_group(const char *name)
{
	/* Section headers are noise on a green run; show them only with VERBOSE=1. */
	if (getenv("VERBOSE") != NULL) {
		printf("  [%s]\n", name);
	}
}

void t_vec(const char *name, const uint8_t *got, size_t len, const char *expect)
{
	char hex[321];

	if (len > 160) {
		printf("    FAIL   %-22s (len %zu too long)\n", name, len);
		t_fail++;
		return;
	}
	t_hex(hex, got, len);
	if (expect == NULL || expect[0] == '\0') {
		printf("    RECORD %-22s = %s\n", name, hex);
		t_pending++;
	} else if (strcmp(hex, expect) == 0) {
		t_pass++;
	} else {
		printf("    FAIL   %-22s\n             got = %s\n             exp = %s\n",
		       name, hex, expect);
		t_fail++;
	}
}

void t_u32(const char *name, uint32_t v, const char *expect)
{
	uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8),
			 (uint8_t)v };

	t_vec(name, b, 4, expect);
}

void t_u16(const char *name, uint16_t v, const char *expect)
{
	uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };

	t_vec(name, b, 2, expect);
}

void t_ok_(const char *name, int cond, const char *file, int line)
{
	if (cond) {
		t_pass++;
	} else {
		printf("    FAIL   %-22s  (%s:%d)\n", name, file, line);
		t_fail++;
	}
}

void t_eqi_(const char *name, long got, long want, const char *file, int line)
{
	if (got == want) {
		t_pass++;
	} else {
		printf("    FAIL   %-22s  got=%ld want=%ld  (%s:%d)\n", name, got, want,
		       file, line);
		t_fail++;
	}
}
