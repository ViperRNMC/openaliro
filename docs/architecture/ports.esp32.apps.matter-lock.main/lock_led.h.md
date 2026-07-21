<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/matter-lock/main/lock_led.h`

Lock status LED color mapping: derives the RGB color for the lock indicator from the
current locked and Aliro-ranging state.

**used by** [`ports/esp32/apps/matter-lock/main/app_driver.cpp`](app_driver.cpp.md), [`ports/esp32/apps/matter-lock/main/lock_led.c`](lock_led.c.md)

## API

### `struct lock_led_rgb`
`ports/esp32/apps/matter-lock/main/lock_led.h:25`

A colour for the single indicator pixel. All-zero means dark.
