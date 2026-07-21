<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp`

AliroReaderDelegate: implements the Aliro reader-provisioning and BLE-UWB portions of the Matter
DoorLock::Delegate interface, backing the controller-facing GetAliro*/SetAliroReaderConfig
commands and persisting the provisioned reader identity via aliro_reader_provision_identity.
Bridges Matter cluster commands to the underlying aliro_reader NVS-backed identity/trust store
and to the BLE advertising layer (refreshed when the group resolving key changes).

**depends on** [`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.h`](aliro_reader_delegate.h.md)

## API

### `void AliroReaderDelegate::EnsureSubIdentifier()`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:51`

Lazily generates and caches the Aliro group sub-identifier via DRBG on first call; subsequent calls are a no-op. On RNG failure, zeroes mGroupSubIdentifier and logs an error, but still marks it as set so this is not retried.

**called by** `GetAliroReaderGroupSubIdentifier`, `Init`, `SetAliroReaderConfig`

### `void AliroReaderDelegate::Init()`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:65`

Generate/settle the reader group sub-identifier. Call once from app_main
(after nvs_flash_init, before esp_matter::start).

**calls** `EnsureSubIdentifier`

### `CHIP_ERROR AliroReaderDelegate::GetAliroReaderVerificationKey(MutableByteSpan &verificationKey)`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:76`

DoorLock::Delegate — Aliro reader-provisioning interface

### `CHIP_ERROR AliroReaderDelegate::GetAliroReaderGroupIdentifier(MutableByteSpan &groupIdentifier)`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:86`

Copies the Aliro reader group identifier into groupIdentifier. If the reader is not configured, reduces the output span to size 0 instead of copying. Returns CHIP_NO_ERROR on success or whatever CopySpanToMutableSpan reports on failure.

### `GetAliroReaderGroupSubIdentifier`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:98`

Copies the Aliro reader group sub-identifier into groupSubIdentifier.
Lazily generates the sub-identifier on first call via EnsureSubIdentifier. Returns CHIP_NO_ERROR
on success or whatever CopySpanToMutableSpan reports on failure.

**calls** `EnsureSubIdentifier`

### `CHIP_ERROR AliroReaderDelegate::CopyProtocolVersionIntoSpan(uint16_t value, MutableByteSpan &out)`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:106`

Encodes a 16-bit Aliro protocol version as 2 big-endian bytes into out. Returns CHIP_ERROR_INVALID_ARGUMENT if out is smaller than kAliroProtocolVersionSize; on success, reduces out to the written size.

**called by** `GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex`, `GetAliroSupportedBLEUWBProtocolVersionAtIndex`

### `CHIP_ERROR AliroReaderDelegate::GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex(size_t index, MutableByteSpan &protocolVersion)`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:123`

Reports the Aliro expedited-transaction protocol version supported at index.
Only index 0 is valid; returns CHIP_ERROR_PROVIDER_LIST_EXHAUSTED for any other index. On
success, encodes kKnownProtocolVersion big-endian into protocolVersion via
CopyProtocolVersionIntoSpan.

**calls** `CopyProtocolVersionIntoSpan`

### `CHIP_ERROR AliroReaderDelegate::GetAliroGroupResolvingKey(MutableByteSpan &groupResolvingKey)`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:133`

Copies the Aliro group resolving key into groupResolvingKey. If the reader is not configured, reduces the output span to size 0 instead of copying. Returns CHIP_NO_ERROR on success or whatever CopySpanToMutableSpan reports on failure.

### `GetAliroSupportedBLEUWBProtocolVersionAtIndex`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:146`

Reports the Aliro BLE-UWB protocol version supported at index.
Only index 0 is valid; returns CHIP_ERROR_PROVIDER_LIST_EXHAUSTED for any other index. On
success, encodes kKnownProtocolVersion big-endian into protocolVersion via
CopyProtocolVersionIntoSpan.

**calls** `CopyProtocolVersionIntoSpan`

### `uint8_t AliroReaderDelegate::GetAliroBLEAdvertisingVersion()`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:157`

Returns the Aliro BLE advertising version. Always 0, the only version currently defined.

### `uint16_t AliroReaderDelegate::GetNumberOfAliroCredentialIssuerKeysSupported()`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:164`

Returns the number of Aliro credential issuer keys supported, kAliroKeysSupported.

### `uint16_t AliroReaderDelegate::GetNumberOfAliroEndpointKeysSupported()`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:170`

Returns the number of Aliro endpoint keys supported, kAliroKeysSupported.

### `CHIP_ERROR AliroReaderDelegate::SetAliroReaderConfig(const ByteSpan &signingKey, const ByteSpan &verificationKey, const ByteSpan &groupIdentifier, const Optional<ByteSpan> &groupResolvingKey)`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:192`

Store a new Aliro reader configuration (signing key, verification key, group identifier, and
optional group resolving key) sent by the controller, and persist the corresponding reader
identity to NVS.
Requires signingKey, verificationKey, and groupIdentifier to each match their fixed expected
sizes, and if present, groupResolvingKey to match its fixed size; returns CHIP_ERROR_INVALID_ARGUMENT
otherwise, without modifying any stored state. If groupResolvingKey is absent, zeroes
mGroupResolvingKey. On success, marks the reader configured, ensures the group sub-identifier is
generated, builds reader_id = groupIdentifier || groupSubIdentifier, and calls
aliro_reader_provision_identity to persist it alongside the signing key and group resolving key
(persistence failure is logged but does not change the return value). Also refreshes the BLE
advertisement so it carries the newly configured group resolving key, since the reader may have
started advertising before this command arrived. Always returns CHIP_NO_ERROR once the size
checks pass.

**calls** `EnsureSubIdentifier`

### `CHIP_ERROR AliroReaderDelegate::ClearAliroReaderConfig()`
`ports/esp32/apps/matter-lock/main/lock/aliro_reader_delegate.cpp:243`

Clears the stored Aliro reader configuration (signing/verification keys, group identifier, group resolving key) and marks the reader unconfigured. Also clears the persisted provisioning state via aliro_reader_provision_clear. Always returns CHIP_NO_ERROR.
