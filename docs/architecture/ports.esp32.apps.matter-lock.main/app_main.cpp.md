<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/matter-lock/main/app_main.cpp`

Matter application main: door lock endpoint setup, Matter lifecycle event handling, and (when
CONFIG_ENABLE_ALIRO_BLE_UWB is set) startup/coexistence wiring for the Aliro BLE+UWB reader
alongside the Matter BLE commissioning transport.
Owns the Aliro reader background task (started once on commissioning-complete or at boot if
already commissioned) and the Matter attribute/identify/device-event callbacks required by
esp-matter's node/cluster framework.

**depends on** [`ports/esp32/apps/matter-lock/main/app_priv.h`](app_priv.h.md), [`ports/esp32/apps/matter-lock/main/app_shell.h`](app_shell.h.md), [`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.h`](../ports.esp32.apps.matter-lock.main.lock/aliro_reader_delegate.h.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](../ports.esp32.apps.matter-lock.main.lock/door_lock_manager.h.md)  ·  **discussed in** [`docs/approach-direction.md`](../../approach-direction.md)

## API

### `static app::DataModel::Nullable<uint16_t> aliro_operating_user(void)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:128`

Resolve the Matter user that owns the credential the reader authenticated, so the LockOperation
event names who operated the lock. Without it the event is anonymous and Apple Home, unable to
tell which member unlocked, notifies every device in the home including the one that just did it.
Call from the Matter task (it reads the door lock's user and credential tables).
Returns a null user index if no credential has authenticated since boot or no stored user owns it.

**called by** `schedule_bolt_lock`, `schedule_bolt_unlock`

### `static void schedule_bolt_unlock(void)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:150`

Drive the bolt from the approach controller. Both hop to the Matter task (the only thread allowed
to touch the DoorLock cluster). Unlock also stamps the walk-up latency mark on its first execution.

**called by** `aliro_reader_task`  ·  **calls** `aliro_operating_user`

### `static int32_t aliro_range_median(const int32_t *win, int n)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:175`

Median of the first n samples of win (n in [1, ALIRO_RANGE_MEDIAN_N]). Rejects the metre-scale
spikes in the per-block UWB distance without the lag of a running average.

**called by** `aliro_reader_task`

### `static void on_uwb_range(void)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:197`

Range-latch listener: runs on the UWB RX path, so it only stamps the latency
trace and wakes the reader task; the unlock decision itself stays on the task.

### `static void aliro_reader_task(void *arg)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:222`

Background task that starts the Aliro reader and drives approach-based lock/unlock from UWB range.
Waits for the shared NimBLE host to be usable (host synced, Matter's advertiser released) before
calling aliro_reader_start_attached(), instead of sleeping a flat second. Then runs forever as the
sole auto-lock driver (the fixed Matter auto-relock is disabled). See the controller comment inside
for how the noisy per-block UWB distance is conditioned into stable grant / unlock / relock events.

**calls** `aliro_range_median`, `schedule_bolt_lock`, `schedule_bolt_unlock`

### `static void start_aliro_reader_once(void)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:341`

Start the Aliro reader task exactly once, idempotent across repeated calls (e.g. from multiple
event callbacks). Spawns aliro_reader_task on its own FreeRTOS task; logs the outcome.

**called by** `app_event_cb`, `app_main`

### `static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:361`

Matter device-event callback: logs commissioning/fabric/BLE lifecycle events and, when Aliro
BLE+UWB support is enabled, starts the Aliro reader once commissioning completes (Matter releases
the BLE advertiser at that point). On the last fabric being removed, reopens a DNS-SD-only
commissioning window if one is not already open.

**calls** `start_aliro_reader_once`

### `static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id, uint8_t effect_variant, void *priv_data)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:456`

This callback is invoked when clients interact with the Identify Cluster.
In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or
light).

### `static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)`
`ports/esp32/apps/matter-lock/main/app_main.cpp:467`

This callback is called for every attribute update. The callback implementation shall
handle the desired attributes and return an appropriate error code. If the attribute
is not of your interest, please do not return an error code and strictly return ESP_OK.

### `extern "C" void app_main()`
`ports/esp32/apps/matter-lock/main/app_main.cpp:490`

Application entry point: initializes NVS, the lock LED, power management, and the Matter node
with a door lock endpoint (adding Aliro provisioning/BLE-UWB clusters and delegate when enabled).
Registers the Aliro reader's GATT service with the BLE host before esp_matter::start so it
coexists with CHIPoBLE. Starts Matter, prints onboarding codes, and if already commissioned (e.g.
after a reboot) starts the Aliro reader immediately; otherwise the reader starts on the
kCommissioningComplete event. Finally launches the interactive console (app_shell_start), which
must not run alongside esp_matter::console::init since both read the same console UART.

**calls** `start_aliro_reader_once`

<details><summary>Undocumented (1)</summary>

- `schedule_bolt_lock`

</details>
