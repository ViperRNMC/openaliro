// Lock status LED color mapping: derives the RGB color for the lock indicator from the
// current locked and Aliro-ranging state.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Bolt-state indicator policy: what colour the LED shows for a given lock
 * state. Kept free of ESP dependencies so it can be exercised on the host;
 * the driver that pushes this onto the WS2812 lives in app_driver.cpp.
 */
#ifndef LOCK_LED_H
#define LOCK_LED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full scale is blinding at arm's length. */
#define LOCK_LED_BRIGHTNESS 32

/* A colour for the single indicator pixel. All-zero means dark. */
struct lock_led_rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

/* Locked extinguishes the indicator. Unlocked lights blue when the UWB
 * approach path drove it and green otherwise, so an approach-unlock is
 * distinguishable from a Home-app tap at a glance. @p aliro is ignored
 * when @p locked is true.
 */
struct lock_led_rgb lock_led_color(bool locked, bool aliro);

#ifdef __cplusplus
}
#endif

#endif /* LOCK_LED_H */
