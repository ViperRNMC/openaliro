/**
 * @file test.h — shared host-test harness: assertions + suite registry.
 *
 * Self-validating helpers first (no golden needed): T_OK/T_EQ for booleans and
 * integers, t_vec round-trip compares. For exact-byte pins use t_vec/t_u32 with
 * an empty expect string to RECORD the value, then bake it in (behavior-lock).
 * Each module under test exposes one void test_<module>(void) suite; test_main.c
 * runs them all and prints an aggregate PASS/FAIL/RECORD.
 */
#ifndef WOZ_HOST_TEST_H
#define WOZ_HOST_TEST_H

#include <stddef.h>
#include <stdint.h>

/* Reporting counters (defined in test.c). */
extern int t_fail;
extern int t_pending;
extern int t_pass;

/* hex helpers */
void t_hex(char *dst, const uint8_t *b, size_t n);
int t_unhex(uint8_t *dst, const char *hex, size_t cap);

/* Group header inside a suite. */
void t_group(const char *name);

/* Byte-vector check. expect==NULL or "" => RECORD (print value, mark pending). */
void t_vec(const char *name, const uint8_t *got, size_t len, const char *expect);
void t_u32(const char *name, uint32_t v, const char *expect);
void t_u16(const char *name, uint16_t v, const char *expect);

/* Self-validating assertions (no golden). */
void t_ok_(const char *name, int cond, const char *file, int line);
void t_eqi_(const char *name, long got, long want, const char *file, int line);
#define T_OK(name, cond)      t_ok_((name), (cond), __FILE__, __LINE__)
#define T_EQ(name, got, want) t_eqi_((name), (long)(got), (long)(want), __FILE__, __LINE__)

/* Module suites — one per file under test. */
void test_ccc_kdf(void);
void test_ccc_mac(void);
void test_ccc_sts(void);
void test_ccc_shim(void);
void test_ccc_session(void);
void test_aliro_builder(void);
void test_aliro_parser(void);
void test_aliro_adapter(void);
void test_aliro_msg(void);
void test_aliro_session(void);
void test_cherry(void);
void test_fira(void);
void test_facade(void);
void test_prepoll_gate(void);
void test_prepoll_round(void);

#endif /* WOZ_HOST_TEST_H */
