/** @file trace.h — Structured [WOZ_TRACE] emit helpers, gated on CONFIG_WOZ_E2E_TRACE. */

#ifndef WOZ_TRACE_H
#define WOZ_TRACE_H

#include <stddef.h>
#include <stdint.h>

#if defined(CONFIG_WOZ_E2E_TRACE)

#include <zephyr/logging/log.h>

/** @brief Buffer length for a 16-char hex prefix (8 bytes) + NUL terminator. */
#define WOZ_TRACE_HEX8_LEN 17

/** @brief Format up to 8 bytes of @p bytes as lowercase hex into @p buf; returns @p buf. */
static inline const char *woz_trace_hex8(char buf[WOZ_TRACE_HEX8_LEN],
                                          const uint8_t *bytes,
                                          size_t len)
{
	static const char nybble[16] = "0123456789abcdef";
	size_t n = (len < 8) ? len : 8;
	for (size_t i = 0; i < n; i++) {
		buf[2 * i]     = nybble[(bytes[i] >> 4) & 0x0f];
		buf[2 * i + 1] = nybble[bytes[i] & 0x0f];
	}
	buf[2 * n] = '\0';
	return buf;
}

/** @brief Emit one trace line via the firmware's LOG_INF backend. */
#define WOZ_TRACE(stage, fmt, ...) \
	LOG_INF("[WOZ_TRACE] src=lock stage=" stage " " fmt, ##__VA_ARGS__)

#else /* !CONFIG_WOZ_E2E_TRACE */

#include <zephyr/sys/printk.h>

#define WOZ_TRACE_HEX8_LEN 17

/** @brief No-op trace macro (if(0)-guarded) that still type-checks its arguments. */
#define WOZ_TRACE(stage, fmt, ...)                                           \
	do {                                                                  \
		if (0) {                                                      \
			printk("[WOZ_TRACE] src=lock stage=" stage " " fmt,  \
			       ##__VA_ARGS__);                                \
		}                                                             \
	} while (0)

/** @brief Stub that touches @p buf and @p bytes so neither becomes unused. */
static inline const char *woz_trace_hex8(char buf[WOZ_TRACE_HEX8_LEN],
                                          const uint8_t *bytes,
                                          size_t len)
{
	(void)bytes;
	(void)len;
	buf[0] = '\0';
	return buf;
}

#endif /* CONFIG_WOZ_E2E_TRACE */

#endif /* WOZ_TRACE_H */
