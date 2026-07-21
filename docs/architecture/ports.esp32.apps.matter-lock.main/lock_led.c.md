<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/matter-lock/main/lock_led.c`

Lock-state indicator LED: maps lock state (and Aliro activity) to an RGB colour for the single
status pixel.
Locked always extinguishes the indicator; unlocked shows blue during active UWB/Aliro engagement
and a different colour otherwise, per lock_led_color.

**depends on** [`ports/esp32/apps/matter-lock/main/lock_led.h`](lock_led.h.md)

## API

### `struct lock_led_rgb lock_led_color(bool locked, bool aliro)`
`ports/esp32/apps/matter-lock/main/lock_led.c:11`

Locked extinguishes the indicator. Unlocked lights blue when the UWB
approach path drove it and green otherwise, so an approach-unlock is
distinguishable from a Home-app tap at a glance. @p aliro is ignored
when @p locked is true.
