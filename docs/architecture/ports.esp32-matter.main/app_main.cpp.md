<!-- generated documentation — edit the source, not this file -->
# `ports/esp32-matter/main/app_main.cpp`

Matter application main: door lock endpoint setup, Matter lifecycle event handling, and (when
CONFIG_ENABLE_ALIRO_BLE_UWB is set) startup/coexistence wiring for the Aliro BLE+UWB reader
alongside the Matter BLE commissioning transport.
Owns the Aliro reader background task (started once on commissioning-complete or at boot if
already commissioned) and the Matter attribute/identify/device-event callbacks required by
esp-matter's node/cluster framework.

**depends on** [`ports/esp32-matter/main/app_priv.h`](app_priv.h.md), [`ports/esp32-matter/main/app_shell.h`](app_shell.h.md), [`ports/esp32-matter/main/lock/aliro_reader_delegate.h`](../ports.esp32-matter.main.lock/aliro_reader_delegate.h.md), [`ports/esp32-matter/main/lock/door_lock_manager.h`](../ports.esp32-matter.main.lock/door_lock_manager.h.md)

## API

### `static void aliro_reader_task(void *arg)`
`ports/esp32-matter/main/app_main.cpp:88`

Background task that starts the Aliro reader and drives approach-based lock/unlock from UWB range.
Delays 1 s at startup to let Matter's BLE host finish syncing before the reader takes over the
shared legacy advertiser, then calls aliro_reader_start_attached().
Runs forever as the sole auto-lock driver (the fixed Matter auto-relock is disabled): unlocks when
a trusted peer's range is within ALIRO_UNLOCK_RANGE_CM, holds while present, and relocks when the
peer moves past ALIRO_RELOCK_RANGE_CM or disconnects. Uses hysteresis (relock band wider than
unlock band) to avoid chatter on range jitter. Sends the phone a "Reader Status Changed" BLE
notification on each transition so the Wallet unlock animation fires; the notification is a no-op
if the ranging session has already dropped. Polls every 200 ms.

### `static void start_aliro_reader_once(void)`
`ports/esp32-matter/main/app_main.cpp:140`

Start the Aliro reader task exactly once, idempotent across repeated calls (e.g. from multiple
event callbacks). Spawns aliro_reader_task on its own FreeRTOS task; logs the outcome.

**called by** `app_event_cb`, `app_main`

### `static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)`
`ports/esp32-matter/main/app_main.cpp:156`

Matter device-event callback: logs commissioning/fabric/BLE lifecycle events and, when Aliro
BLE+UWB support is enabled, starts the Aliro reader once commissioning completes (Matter releases
the BLE advertiser at that point). On the last fabric being removed, reopens a DNS-SD-only
commissioning window if one is not already open.

**calls** `start_aliro_reader_once`

### `static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id, uint8_t effect_variant, void *priv_data)`
`ports/esp32-matter/main/app_main.cpp:242`

This callback is invoked when clients interact with the Identify Cluster.
In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or
light).

### `static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)`
`ports/esp32-matter/main/app_main.cpp:253`

This callback is called for every attribute update. The callback implementation shall
handle the desired attributes and return an appropriate error code. If the attribute
is not of your interest, please do not return an error code and strictly return ESP_OK.

### `extern "C" void app_main()`
`ports/esp32-matter/main/app_main.cpp:276`

Application entry point: initializes NVS, the lock LED, power management, and the Matter node
with a door lock endpoint (adding Aliro provisioning/BLE-UWB clusters and delegate when enabled).
Registers the Aliro reader's GATT service with the BLE host before esp_matter::start so it
coexists with CHIPoBLE. Starts Matter, prints onboarding codes, and if already commissioned (e.g.
after a reboot) starts the Aliro reader immediately; otherwise the reader starts on the
kCommissioningComplete event. Finally launches the interactive console (app_shell_start), which
must not run alongside esp_matter::console::init since both read the same console UART.

**calls** `start_aliro_reader_once`

### `std::vector<struct ble_gatt_svc_def> svcs =`
`ports/esp32-matter/main/app_main.cpp:354`

Local vector wrapping the Aliro GATT service definition for registration with the BLE host's
combined service table.
