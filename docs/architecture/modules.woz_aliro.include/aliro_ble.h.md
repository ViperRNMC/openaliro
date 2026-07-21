<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_ble.h`

Aliro BLE-UWB reader transport: GATT service definition, advertised feature flags, and transport
callbacks connecting the BLE peripheral role to the Aliro protocol handler in aliro_reader.
Callers configure the transport via aliro_ble_prepare (which builds the READ characteristic
payload without touching NimBLE), then register the GATT service returned by
aliro_ble_service_def with the host's combined service table.

**used by** [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)  ·  **discussed in** [`docs/porting.md`](../../porting.md)

## API

### `struct aliro_ble_features`
`modules/woz_aliro/include/aliro_ble.h:26`

Aliro BLE-UWB supported-features flags (advertised in the READ char, and
parsed from the device WRITE). Serialized as one byte: bit0/1/2.

### `struct aliro_ble_callbacks`
`modules/woz_aliro/include/aliro_ble.h:33`

Transport callbacks into the app / Phase-3 Aliro handler. All optional.

### `struct aliro_ble_config`
`modules/woz_aliro/include/aliro_ble.h:44`

Reader configuration. `proto_versions` are host-order uint16s; they are the
provisioned `aliroSupportedBLEUWBProtocolVersions` (Matter attr 133), NOT a
transport constant, so the caller supplies them.
