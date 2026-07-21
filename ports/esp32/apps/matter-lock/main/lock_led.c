// Lock-state indicator LED: maps lock state (and Aliro activity) to an RGB colour for the single
// status pixel.
// Locked always extinguishes the indicator; unlocked shows blue during active UWB/Aliro engagement
// and a different colour otherwise, per lock_led_color.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include "lock_led.h"

struct lock_led_rgb lock_led_color(bool locked, bool aliro)
{
	struct lock_led_rgb c = { 0, 0, 0 };

	if (locked) {
		return c;
	}
	if (aliro) {
		c.b = LOCK_LED_BRIGHTNESS;
	} else {
		c.g = LOCK_LED_BRIGHTNESS;
	}
	return c;
}
