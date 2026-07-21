/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 * woz_log.h - logging contract for the UWB engine.
 *
 * Zephyr's LOG_* spellings are kept so the call sites need only swap an
 * include; on Zephyr this defers to the real header, elsewhere it maps them
 * onto the platform logger. woz_printf replaces bare printk.
 *
 * WOZ_PRINTK_COMPAT: the non-Zephyr branches also alias `printk` itself. That
 * exists for exactly one caller: the DIAG instrumentation this repo added to
 * the vendored DW3000 sources (deca_compat.c, deca_interface.c,
 * dw3000_device.c), which has ~18 printk call sites. Aliasing the spelling
 * keeps those vendor files to a one-line include change instead of rewriting
 * vendor call sites. Our own code uses woz_printf; if the vendor DIAG tracing
 * is ever dropped, this alias goes with it.
 */
#ifndef WOZ_LOG_H
#define WOZ_LOG_H

#if defined(__ZEPHYR__)

#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#define woz_printf(...) printk(__VA_ARGS__)

#elif defined(ESP_PLATFORM)

#include <stdio.h>

#include "esp_log.h"

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERR  1
#define LOG_LEVEL_WRN  2
#define LOG_LEVEL_INF  3
#define LOG_LEVEL_DBG  4

/* Each module's LOG_MODULE_REGISTER/DECLARE(name, ...) fixes a per-file tag. */
#define LOG_MODULE_REGISTER(name, ...)                                                             \
	static const char *const WOZ_LOG_TAG __attribute__((unused)) = #name
#define LOG_MODULE_DECLARE(name, ...)                                                              \
	static const char *const WOZ_LOG_TAG __attribute__((unused)) = #name

#define LOG_ERR(...)    ESP_LOGE(WOZ_LOG_TAG, __VA_ARGS__)
#define LOG_WRN(...)    ESP_LOGW(WOZ_LOG_TAG, __VA_ARGS__)
#define LOG_INF(...)    ESP_LOGI(WOZ_LOG_TAG, __VA_ARGS__)
#define LOG_DBG(...)    ESP_LOGD(WOZ_LOG_TAG, __VA_ARGS__)
#define LOG_PRINTK(...) printf(__VA_ARGS__)

#define LOG_HEXDUMP_ERR(p, l, s) ESP_LOG_BUFFER_HEX_LEVEL(WOZ_LOG_TAG, (p), (l), ESP_LOG_ERROR)
#define LOG_HEXDUMP_WRN(p, l, s) ESP_LOG_BUFFER_HEX_LEVEL(WOZ_LOG_TAG, (p), (l), ESP_LOG_WARN)
#define LOG_HEXDUMP_INF(p, l, s) ESP_LOG_BUFFER_HEX_LEVEL(WOZ_LOG_TAG, (p), (l), ESP_LOG_INFO)
#define LOG_HEXDUMP_DBG(p, l, s) ESP_LOG_BUFFER_HEX_LEVEL(WOZ_LOG_TAG, (p), (l), ESP_LOG_DEBUG)

#define woz_printf(...) printf(__VA_ARGS__)
#ifndef printk
#define printk(...) printf(__VA_ARGS__) /* see WOZ_PRINTK_COMPAT below */
#endif

#elif defined(WOZ_PORT_HOST)

#include <stdio.h>

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERR  1
#define LOG_LEVEL_WRN  2
#define LOG_LEVEL_INF  3
#define LOG_LEVEL_DBG  4

/* Logging compiles out to no-ops off-target: log calls have no side effect a
 * unit test observes. Matches the behaviour of the shim this replaces. */
#define LOG_MODULE_REGISTER(...)
#define LOG_MODULE_DECLARE(...)

#define LOG_ERR(...)    ((void)0)
#define LOG_WRN(...)    ((void)0)
#define LOG_INF(...)    ((void)0)
#define LOG_DBG(...)    ((void)0)
#define LOG_PRINTK(...) ((void)0)

#define LOG_HEXDUMP_ERR(...) ((void)0)
#define LOG_HEXDUMP_WRN(...) ((void)0)
#define LOG_HEXDUMP_INF(...) ((void)0)
#define LOG_HEXDUMP_DBG(...) ((void)0)

/* printk did map to stdio in the host shim, and some diagnostics are asserted
 * on in test output, so keep woz_printf real here. */
#define woz_printf(...)      printf(__VA_ARGS__)
#ifndef printk
#define printk(...) printf(__VA_ARGS__) /* see WOZ_PRINTK_COMPAT below */
#endif

#else
#error "woz_log.h: no platform backend. Define WOZ_PORT_HOST, or build under Zephyr/ESP-IDF."
#endif

#endif /* WOZ_LOG_H */
