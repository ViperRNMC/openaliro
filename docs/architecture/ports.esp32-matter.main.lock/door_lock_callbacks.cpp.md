<!-- generated documentation — edit the source, not this file -->
# `ports/esp32-matter/main/lock/door_lock_callbacks.cpp`

Matter DoorLock cluster plugin callbacks: wires the ESP32 port's BoltLockManager into the
Matter DoorLock cluster's lock/unlock commands, user and credential storage, schedule
storage, cluster init, and auto-relock notification hooks.

**depends on** [`ports/esp32-matter/main/lock/door_lock_manager.h`](door_lock_manager.h.md)

## API

### `void door_lock_init()`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:22`

Log that the door lock example has initialized. Performs no other setup.

### `void emberAfDoorLockClusterInitCallback(EndpointId endpoint)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:31`

Matter DoorLock cluster init callback: initializes the cluster server for the
endpoint, forces its LockState attribute to Locked, and syncs the bolt
manager's lock state from BoltLockMgr().InitLockState(), logging an error if
that sync fails.

### `bool emberAfPluginDoorLockOnDoorLockCommand(chip::EndpointId endpointId, const Nullable<chip::FabricIndex> &fabricIdx, const Nullable<chip::NodeId> &nodeId, const Optional<ByteSpan> &pinCode, OperationErrorEnum &err)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:46`

Matter DoorLock plugin hook for a remote Lock command.
Validates the supplied PIN via BoltLockMgr().ValidatePIN, writing any failure
reason to err. On success, locks the bolt with OperationSourceEnum::kRemote.
Returns the validation result; on false, err holds the failure reason and the
lock is left untouched.

### `bool emberAfPluginDoorLockOnDoorUnlockCommand(chip::EndpointId endpointId, const Nullable<chip::FabricIndex> &fabricIdx, const Nullable<chip::NodeId> &nodeId, const Optional<ByteSpan> &pinCode, OperationErrorEnum &err)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:66`

Matter DoorLock plugin hook for a remote Unlock command.
Validates the supplied PIN via BoltLockMgr().ValidatePIN, writing any failure
reason to err. On success, unlocks the bolt with
OperationSourceEnum::kRemote. Returns the validation result; on false, err
holds the failure reason and the lock is left untouched.

### `bool emberAfPluginDoorLockGetCredential(chip::EndpointId endpointId, uint16_t credentialIndex, CredentialTypeEnum credentialType, EmberAfPluginDoorLockCredentialInfo &credential)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:83`

Matter DoorLock plugin hook: fetch a stored credential by index and type for
an endpoint. Delegates to BoltLockMgr().GetCredential; returns true if found.

### `bool emberAfPluginDoorLockSetCredential(chip::EndpointId endpointId, uint16_t credentialIndex, chip::FabricIndex creator, chip::FabricIndex modifier, DlCredentialStatus credentialStatus, CredentialTypeEnum credentialType, const chip::ByteSpan &credentialData)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:98`

Matter DoorLock plugin hook: store a credential for an endpoint via
BoltLockMgr().SetCredential.
When CONFIG_ENABLE_ALIRO_BLE_UWB is enabled and the write succeeds with an
Occupied status, a 65-byte Aliro endpoint key (evictable or non-evictable),
the raw key is additionally mirrored into the Aliro reader's trust store via
aliro_reader_provision_add_trust, so the reader accepts ranging auth from the
Wallet credential Apple just installed. Returns the underlying
SetCredential result regardless of whether the mirror step ran.

### `bool emberAfPluginDoorLockGetUser(chip::EndpointId endpointId, uint16_t userIndex, EmberAfPluginDoorLockUserInfo &user)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:124`

Matter DoorLock plugin hook: fetch a stored user by index for an endpoint.
Delegates to BoltLockMgr().GetUser; returns true if found.

### `bool emberAfPluginDoorLockSetUser(chip::EndpointId endpointId, uint16_t userIndex, chip::FabricIndex creator, chip::FabricIndex modifier, const chip::CharSpan &userName, uint32_t uniqueId, UserStatusEnum userStatus, UserTypeEnum usertype, CredentialRuleEnum credentialRule, const CredentialStruct *credentials, size_t totalCredentials)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:133`

Matter DoorLock plugin hook: store a user record for an endpoint, including
name, unique ID, status, type, credential rule, and its list of credentials.
Delegates to BoltLockMgr().SetUser.

### `DlStatus emberAfPluginDoorLockGetSchedule(chip::EndpointId endpointId, uint8_t holidayIndex, EmberAfPluginDoorLockHolidaySchedule &holidaySchedule)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:162`

Matter DoorLock plugin hook: fetch a holiday schedule entry by index for an
endpoint. Delegates to BoltLockMgr().GetHolidaySchedule.

### `DlStatus emberAfPluginDoorLockSetSchedule(chip::EndpointId endpointId, uint8_t holidayIndex, DlScheduleStatus status, uint32_t localStartTime, uint32_t localEndTime, OperatingModeEnum operatingMode)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:188`

Matter DoorLock plugin hook: store a holiday schedule entry for an endpoint.
Delegates to BoltLockMgr().SetHolidaySchedule.

### `void emberAfPluginDoorLockOnAutoRelock(chip::EndpointId endpointId)`
`ports/esp32-matter/main/lock/door_lock_callbacks.cpp:198`

Matter DoorLock plugin hook invoked on auto-relock; logs the event only, no
lock-state change is performed here.
