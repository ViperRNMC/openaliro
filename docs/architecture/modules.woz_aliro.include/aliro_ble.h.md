<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_ble.h`

Aliro BLE-UWB reader transport: GATT service definition, advertised feature flags, and transport
callbacks connecting the BLE peripheral role to the Aliro protocol handler in aliro_reader.
Callers configure the transport via aliro_ble_prepare (which builds the READ characteristic
payload without touching NimBLE), then register the GATT service returned by
aliro_ble_service_def with the host's combined service table.

**used by** [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)

## API

### `struct aliro_ble_features`
`modules/woz_aliro/include/aliro_ble.h:26`

Aliro BLE-UWB supported-features flags (advertised in the READ char, and
parsed from the device WRITE). Serialized as one byte: bit0/1/2.

### `struct aliro_ble_callbacks`
`modules/woz_aliro/include/aliro_ble.h:33`

Transport callbacks into the app / Phase-3 Aliro handler. All optional.

#### `struct aliro_ble_features`
`modules/woz_aliro/include/aliro_ble.h:47`

Aliro BLE-UWB supported-features flags (advertised in the READ char, and
parsed from the device WRITE). Serialized as one byte: bit0/1/2.

#### `struct aliro_ble_callbacks`
`modules/woz_aliro/include/aliro_ble.h:48`

Transport callbacks into the app / Phase-3 Aliro handler. All optional.

### `int aliro_ble_prepare(const struct aliro_ble_config *cfg)`
`modules/woz_aliro/include/aliro_ble.h:76`

Capture config + build the READ payload; does NOT touch NimBLE. 0 on ok.

### `const struct ble_gatt_svc_def *aliro_ble_service_def(void)`
`modules/woz_aliro/include/aliro_ble.h:80`

The Aliro GATT service definition, to hand to the host owner's
register-extra-services hook. Valid after aliro_ble_prepare().
