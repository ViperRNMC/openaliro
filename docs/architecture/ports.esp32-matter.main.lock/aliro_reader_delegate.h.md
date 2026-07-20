<!-- generated documentation — edit the source, not this file -->
# `ports/esp32-matter/main/lock/aliro_reader_delegate.h`

Declares AliroReaderDelegate, the Aliro (Apple Home Key) reader-provisioning and BLE-UWB half of
the Matter DoorLock cluster delegate, bridging controller commands to the on-device reader
identity, trust store, and BLE advertising state.

**used by** [`ports/esp32-matter/main/app_main.cpp`](../ports.esp32-matter.main/app_main.cpp.md), [`ports/esp32-matter/main/lock/aliro_reader_delegate.cpp`](aliro_reader_delegate.cpp.md)

## API

### `class AliroReaderDelegate: public chip::app::Clusters::DoorLock::Delegate`
`ports/esp32-matter/main/lock/aliro_reader_delegate.h:50`

AliroReaderDelegate — the Aliro (Apple Home Key) reader-provisioning half of
the Door Lock cluster Delegate, for a BLE + UWB ("Express") reader.
Apple Home writes the reader identity with SetAliroReaderConfig (signing key,
verification key, group identifier, and — because kAliroBLEUWB is advertised —
the group resolving key) and reads the reader attributes back to confirm; this
delegate holds that config and advertises the expedited + BLE-UWB protocol
versions the reader speaks.
Aliro credential *keys* (issuer / endpoint, credential types 6/7/8) do NOT
flow through this Delegate. They arrive on the generic
emberAfPluginDoorLockSet/GetCredential path and are stored by BoltLockManager
(its storage is credential-type indexed and already sized to hold them), so
this class implements only the reader-config attributes plus the
"number of keys supported" getters the server consults to validate those
credential writes.
The provisioned identity is persisted, not in-memory only: SetAliroReaderConfig
hands it to aliro_reader_provision_identity, which stores it through aliro_prov
in NVS and refreshes advertising. That is what keeps a Wallet key valid and lets
the UWB reader start after a power cycle.
