// Board driver glue for the ESP32 Matter port: button input, WS2812 lock-status LED, and the
// Matter attribute-update hook wired into the app's driver layer.
/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include "bsp/esp-bsp.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "lock_led.h"

#include <app_priv.h>

static const char *TAG = "app_driver";

/* Single WS2812 on the ESP32-S3-WROOM N16R8 devkit. GPIO48 per the board
 * pinout; clear of the DW3000 (GPIO 4,5,6,10-13) and of octal PSRAM (33-37). */
#define LOCK_LED_GPIO 48

static led_strip_handle_t s_lock_led;

using namespace chip::app::Clusters;
using namespace esp_matter;

// Attribute-update hook for the app driver. Currently a stub: always returns
// ESP_OK without applying val to the given endpoint/cluster/attribute.
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    return err;
}

// Initialize the board's buttons via the BSP button driver and return a handle
// to the first button.
// Caller must treat the returned handle as opaque; only one button (index 0) is
// exposed even if BSP_BUTTON_NUM is larger.
app_driver_handle_t app_driver_button_init()
{
    /* Initialize button */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));

    return (app_driver_handle_t)btns[0];
}

// Initialize the single-pixel WS2812 lock status LED over RMT.
// On failure, logs the error, leaves the internal LED handle NULL (so
// app_driver_led_lock_state becomes a silent no-op), and returns the
// led_strip_new_rmt_device error code. On success, clears the LED and returns
// that call's result.
esp_err_t app_driver_led_init()
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LOCK_LED_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {},
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags = {},
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_lock_led);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lock LED init failed: %d", err);
        s_lock_led = NULL;
        return err;
    }
    return led_strip_clear(s_lock_led);
}

// Set the lock status LED color to reflect lock and Aliro state.
// No-op if app_driver_led_init failed or was never called. Looks up the RGB
// color for the given (locked, aliro) combination and pushes it to the single
// pixel.
void app_driver_led_lock_state(bool locked, bool aliro)
{
    if (s_lock_led == NULL) {
        return; /* init failed or was never called: stay silent */
    }
    // RGB color for the lock status LED corresponding to a given locked/Aliro state
    // combination, as returned by lock_led_color.
    struct lock_led_rgb c = lock_led_color(locked, aliro);
    led_strip_set_pixel(s_lock_led, 0, c.r, c.g, c.b);
    led_strip_refresh(s_lock_led);
}
