<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/matter-lock/main/app_driver.cpp`

Board driver glue for the ESP32 Matter port: button input, WS2812 lock-status LED, and the
Matter attribute-update hook wired into the app's driver layer.

**depends on** [`ports/esp32/apps/matter-lock/main/app_priv.h`](app_priv.h.md), [`ports/esp32/apps/matter-lock/main/lock_led.h`](lock_led.h.md)

## API

### `esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val)`
`ports/esp32/apps/matter-lock/main/app_driver.cpp:36`

Attribute-update hook for the app driver. Currently a stub: always returns
ESP_OK without applying val to the given endpoint/cluster/attribute.

### `app_driver_handle_t app_driver_button_init()`
`ports/esp32/apps/matter-lock/main/app_driver.cpp:47`

Initialize the board's buttons via the BSP button driver and return a handle
to the first button.
Caller must treat the returned handle as opaque; only one button (index 0) is
exposed even if BSP_BUTTON_NUM is larger.

### `esp_err_t app_driver_led_init()`
`ports/esp32/apps/matter-lock/main/app_driver.cpp:61`

Initialize the single-pixel WS2812 lock status LED over RMT.
On failure, logs the error, leaves the internal LED handle NULL (so
app_driver_led_lock_state becomes a silent no-op), and returns the
led_strip_new_rmt_device error code. On success, clears the LED and returns
that call's result.

### `void app_driver_led_lock_state(bool locked, bool aliro)`
`ports/esp32/apps/matter-lock/main/app_driver.cpp:90`

Set the lock status LED color to reflect lock and Aliro state.
No-op if app_driver_led_init failed or was never called. Looks up the RGB
color for the given (locked, aliro) combination and pushes it to the single
pixel.
