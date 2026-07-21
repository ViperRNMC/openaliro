<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp`

BoltLockManager: Matter door lock cluster backing store for the ESP32 port. Implements the DoorLock cluster's user, credential, and weekday/yearday/holiday schedule get/set callbacks over fixed-size in-memory tables mirrored to NVM (ESP32Config blobs), plus lock/unlock actuation and PIN validation. Cluster indices are one-indexed by Matter and decremented internally before bounds-checking against this platform's fixed capacity limits.

**depends on** [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`](door_lock_manager.h.md)

## API

### `bool ReadOptionalConfigBlob(ESP32Config::Key key, uint8_t *data, size_t dataLen, bool *outStale)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:39`

Read an optional fixed-size config blob into data. Treats "config not found" as success (the blob
is optional and data is left untouched), logs and returns false on any other read error.
A size mismatch means the blob was written by a build whose record layout differs from this one
(for example EmberAfPluginDoorLockUserInfo growing across a CHIP update). That is a stale on-disk
format, not a read failure: the partially-filled buffer is unusable, so *outStale is set and the
caller reinitialises and rewrites every table. Reporting it as a failure instead would fail
InitLockState() on every boot with no path back to a good state.

**called by** `ReadConfigValues`

### `bool WriteConfigBlob(ESP32Config::Key key, const uint8_t *data, size_t dataLen)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:59`

Write a fixed-size config blob to persistent storage. Logs and returns false on write failure.

**called by** `ReadConfigValues`, `SetCredential`, `SetHolidaySchedule`, `SetUser`, `SetWeekdaySchedule`, `SetYeardaySchedule`

### `CHIP_ERROR BoltLockManager::Init(DataModel::Nullable<DoorLock::DlLockState> state, LockParam lockParam)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:75`

Validate the configured lock parameters against this platform's fixed capacity limits
(users, credentials per user, weekday/yearday schedules per user, holiday schedules) and store
them. Returns CHIP_ERROR_NO_MEMORY if any configured count exceeds its platform maximum, otherwise
CHIP_NO_ERROR. The state parameter is accepted but unused.

**called by** `InitLockState`

### `bool BoltLockManager::IsValidUserIndex(uint16_t userIndex)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:113`

Check whether userIndex is a valid user index (less than kMaxUsers).

**called by** `GetUser`, `GetWeekdaySchedule`, `GetYeardaySchedule`, `SetUser`, `SetWeekdaySchedule`, `SetYeardaySchedule`

### `bool BoltLockManager::IsValidCredentialIndex(uint16_t credentialIndex, CredentialTypeEnum type)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:120`

Check whether credentialIndex is valid for the given credential type. Programming PIN credentials
require index 0; all other types require an index less than kMaxCredentialsPerUser.

**called by** `GetCredential`, `SetCredential`, `UserIndexForAliroCredential`

### `uint16_t BoltLockManager::CredentialStorageIndex(uint16_t credentialIndex, CredentialTypeEnum type) const`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:130`

Compute the flat storage index for a credential of the given type and per-user index, packing
type and index into a single slot number (type * kMaxCredentialsPerUser + credentialIndex).

**called by** `GetCredential`, `SetCredential`, `UserIndexForAliroCredential`, `ValidatePIN`

### `bool BoltLockManager::IsValidWeekdayScheduleIndex(uint8_t scheduleIndex)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:137`

Check whether scheduleIndex is a valid weekday schedule index (less than
kMaxWeekdaySchedulesPerUser).

**called by** `GetWeekdaySchedule`, `SetWeekdaySchedule`

### `bool BoltLockManager::IsValidYeardayScheduleIndex(uint8_t scheduleIndex)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:144`

Check whether scheduleIndex is a valid yearday schedule index (less than
kMaxYeardaySchedulesPerUser).

**called by** `GetYeardaySchedule`, `SetYeardaySchedule`

### `bool BoltLockManager::IsValidHolidayScheduleIndex(uint8_t scheduleIndex)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:150`

Check whether scheduleIndex is a valid holiday schedule index (less than kMaxHolidaySchedules).

**called by** `GetHolidaySchedule`, `SetHolidaySchedule`

### `bool BoltLockManager::ReadConfigValues()`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:164`

Loads all persisted door-lock state (users, credentials, names, credential data, per-user credential links, weekday/yearday/holiday schedules) from NVM into the in-memory tables.
Detects a stale database, either by a blob whose stored size does not match this build's table
size or by credential slot 0 holding a non-programming-PIN type (an old mixed-type layout); in
either case clears every in-memory table and rewrites all blobs to NVM.
Returns true if all blob reads (and, if triggered, all stale-format rewrites) succeed; false if any read or rewrite fails.
Every blob is sized by its own array via sizeof, never by the runtime LockParams counts. The
counts come from Matter attributes and can differ from the compiled capacity, which would make
the read length disagree with the write length and re-flag the blob as stale on every boot.

**called by** `InitLockState`  ·  **calls** `ReadOptionalConfigBlob`, `WriteConfigBlob`

### `void BoltLockManager::Lock(EndpointId endpointId, OperationSourceEnum source, const DataModel::Nullable<uint16_t> &userIndex)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:228`

Lock the bolt on the given endpoint, updating the Matter door lock cluster state and the status
LED. Records source as the cause of the lock transition.
userIndex: one-indexed user the operation is attributed to in the LockOperation event, or null
when the operation cannot be traced to a stored user.

### `void BoltLockManager::Unlock(EndpointId endpointId, OperationSourceEnum source, const DataModel::Nullable<uint16_t> &userIndex)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:238`

Unlock the bolt on the given endpoint, updating the Matter door lock cluster state and the status
LED. Drives the LED's Aliro-specific indication when source is kAliro.
userIndex: one-indexed user the operation is attributed to in the LockOperation event, or null
when the operation cannot be traced to a stored user.

### `uint16_t BoltLockManager::UserIndexForAliroCredential(const ByteSpan &credentialData)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:251`

Find the user that owns a given Aliro endpoint key, so an approach unlock can be attributed to a
person in the LockOperation event rather than reported anonymously.
credentialData: the credential public key the reader authenticated (uncompressed P-256, 65 bytes).
Scans occupied users' credential links for an Aliro endpoint-key credential whose stored bytes
match, comparing against the mCredentialData backing store directly (the ByteSpan in
mLockCredentials is restored from NVM with a stale pointer after a reboot).
Returns the one-indexed user index, or 0 if no occupied user owns a matching credential.

**calls** `CredentialStorageIndex`, `IsValidCredentialIndex`

### `bool BoltLockManager::GetUser(EndpointId endpointId, uint16_t userIndex, EmberAfPluginDoorLockUserInfo &user)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:284`

Looks up a stored user by index for the Matter door lock cluster.
userIndex: one-indexed by the caller; decremented here and validated against kMaxUsers.
user: output; on an available (unoccupied) slot only userStatus is set; on an occupied slot name, credentials, unique ID, type, credential rule, and creator/modifier are also filled in, with creation/modification source always reported as Matter.
Returns true if userIndex is valid, whether or not the slot is occupied; returns false only if userIndex (0 or out of range) is invalid.

**calls** `IsValidUserIndex`

### `bool BoltLockManager::SetUser(EndpointId endpointId, uint16_t userIndex, FabricIndex creator, FabricIndex modifier, const CharSpan &userName, uint32_t uniqueId, UserStatusEnum userStatus, UserTypeEnum usertype, CredentialRuleEnum credentialRule, const CredentialStruct * credentials, size_t totalCredentials)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:328`

Stores a user record by index for the Matter door lock cluster, persisting it and its credential links to NVM.
userIndex: one-indexed by the caller; decremented here and validated against kMaxUsers.
creator, modifier: fabric indices recorded as the user's creator and last modifier.
userName: must not exceed DOOR_LOCK_MAX_USER_NAME_SIZE.
credentials, totalCredentials: credential list to associate with the user; totalCredentials must not exceed LockParams.numberOfCredentialsPerUser.
Returns false if userIndex is invalid, userName is too long, totalCredentials is too large, or the NVM write fails; true on success.

**calls** `IsValidUserIndex`, `WriteConfigBlob`

### `bool BoltLockManager::GetCredential(EndpointId endpointId, uint16_t credentialIndex, CredentialTypeEnum credentialType, EmberAfPluginDoorLockCredentialInfo &credential)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:393`

Looks up a stored credential by index and type for the Matter door lock cluster.
Programming-PIN indices are used as-is; all other credential types are one-indexed by the caller and decremented here.
endpointId: unused, present for cluster callback signature compatibility.
credentialIndex: caller-supplied index; must pass IsValidCredentialIndex after the type-specific decrement.
credentialType: type of credential being looked up.
credential: output; on an available (unoccupied) slot only status is set; on an occupied slot type, data, and creator/modifier are also filled in, with creation/modification source always reported as Matter.
Returns true if credentialIndex is valid, whether or not the slot is occupied; returns false only if credentialIndex is invalid for the given type.

**calls** `CredentialStorageIndex`, `IsValidCredentialIndex`

### `bool BoltLockManager::SetCredential(EndpointId endpointId, uint16_t credentialIndex, FabricIndex creator, FabricIndex modifier, DlCredentialStatus credentialStatus, CredentialTypeEnum credentialType, const ByteSpan &credentialData)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:438`

Stores a credential by index and type for the Matter door lock cluster, persisting it to NVM.
Programming-PIN indices are used as-is; all other credential types are one-indexed by the caller and decremented here.
endpointId: unused, present for cluster callback signature compatibility.
credentialIndex: caller-supplied index; must pass IsValidCredentialIndex after the type-specific decrement.
creator, modifier: fabric indices recorded as the credential's creator and last modifier.
credentialStatus: status to store for this credential slot.
credentialType: type of credential being stored.
credentialData: raw credential bytes; must not exceed kMaxCredentialSize.
Returns true on success; false if credentialIndex is invalid, credentialData exceeds kMaxCredentialSize, or the NVM write fails.

**calls** `CredentialStorageIndex`, `IsValidCredentialIndex`, `WriteConfigBlob`

### `DlStatus BoltLockManager::GetWeekdaySchedule(EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex, EmberAfPluginDoorLockWeekDaySchedule &schedule)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:478`

Looks up a stored weekday schedule by weekday index and user index for the Matter door lock cluster.
weekdayIndex, userIndex: both one-indexed by the caller; decremented here and validated against their respective max counts.
schedule: output, populated only when the slot is occupied.
Returns DlStatus::kFailure if either index is 0 or out of range, kNotFound if the slot is unoccupied, kSuccess otherwise.

**calls** `IsValidUserIndex`, `IsValidWeekdayScheduleIndex`

### `DlStatus BoltLockManager::SetWeekdaySchedule(EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex, DlScheduleStatus status, DaysMaskMap daysMask, uint8_t startHour, uint8_t startMinute, uint8_t endHour, uint8_t endMinute)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:504`

Stores a weekday schedule by weekday index and user index for the Matter door lock cluster, persisting it to NVM.
weekdayIndex, userIndex: both one-indexed by the caller; decremented here and validated against their respective max counts.
status, daysMask, startHour, startMinute, endHour, endMinute: schedule fields written to the slot.
Returns DlStatus::kFailure if either index is 0, out of range, or the NVM write fails; kSuccess otherwise.

**calls** `IsValidUserIndex`, `IsValidWeekdayScheduleIndex`, `WriteConfigBlob`

### `DlStatus BoltLockManager::GetYeardaySchedule(EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex, EmberAfPluginDoorLockYearDaySchedule &schedule)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:538`

Looks up a stored yearday schedule by yearday index and user index for the Matter door lock cluster.
yearDayIndex, userIndex: both one-indexed by the caller; decremented here and validated against their respective max counts.
schedule: output, populated only when the slot is occupied.
Returns DlStatus::kFailure if either index is 0 or out of range, kNotFound if the slot is unoccupied, kSuccess otherwise.

**calls** `IsValidUserIndex`, `IsValidYeardayScheduleIndex`

### `DlStatus BoltLockManager::SetYeardaySchedule(EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex, DlScheduleStatus status, uint32_t localStartTime, uint32_t localEndTime)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:564`

Stores a yearday schedule by yearday index and user index for the Matter door lock cluster, persisting it to NVM.
yearDayIndex, userIndex: both one-indexed by the caller; decremented here and validated against their respective max counts.
status, localStartTime, localEndTime: schedule fields written to the slot.
Returns DlStatus::kFailure if either index is 0, out of range, or the NVM write fails; kSuccess otherwise.

**calls** `IsValidUserIndex`, `IsValidYeardayScheduleIndex`, `WriteConfigBlob`

### `DlStatus BoltLockManager::GetHolidaySchedule(EndpointId endpointId, uint8_t holidayIndex, EmberAfPluginDoorLockHolidaySchedule &schedule)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:594`

Looks up a stored holiday schedule by index for the Matter door lock cluster.
holidayIndex: one-indexed by the caller; decremented here and validated against kMaxHolidaySchedules.
schedule: output, populated only when the slot is occupied.
Returns DlStatus::kFailure if holidayIndex is 0 or out of range, kNotFound if the slot is unoccupied, kSuccess otherwise.

**calls** `IsValidHolidayScheduleIndex`

### `DlStatus BoltLockManager::SetHolidaySchedule(EndpointId endpointId, uint8_t holidayIndex, DlScheduleStatus status, uint32_t localStartTime, uint32_t localEndTime, OperatingModeEnum operatingMode)`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:617`

Stores a holiday schedule by index for the Matter door lock cluster, persisting it to NVM.
holidayIndex: one-indexed by the caller; decremented here and validated against kMaxHolidaySchedules.
status, localStartTime, localEndTime, operatingMode: schedule fields written to the slot.
Returns DlStatus::kFailure if holidayIndex is 0, out of range, or the NVM write fails; kSuccess otherwise.

**calls** `IsValidHolidayScheduleIndex`, `WriteConfigBlob`

### `const char * BoltLockManager::lockStateToString(DlLockState lockState) const`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:643`

Return a human-readable string for a DlLockState value ("Not Fully Locked", "Locked", "Unlocked",
"Unlatched"), or "Unknown" for kUnknownEnumValue or any unrecognized value.

### `bool BoltLockManager::ValidatePIN(EndpointId endpointId, const Optional<ByteSpan> &pin, OperationErrorEnum &err) const`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:667`

Validates an optional PIN against the door lock's stored PIN credentials for a remote operation.
If no PIN is supplied, succeeds unless the RequirePINforRemoteOperation attribute is true (defaults to not required if the attribute read fails). If a PIN is supplied, succeeds if it exact-matches any occupied PIN credential slot.
endpointId: endpoint whose RequirePINforRemoteOperation attribute is checked.
pin: PIN to validate, or empty to check whether a PIN is required.
err: output; set to OperationErrorEnum::kInvalidCredential when a supplied PIN matches no stored credential.
Returns true if no PIN was required or a match was found; false otherwise.

**calls** `CredentialStorageIndex`

### `CHIP_ERROR BoltLockManager::InitLockState()`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp:707`

Initializes the door lock's runtime state: reads the initial lock state and per-endpoint user/credential capacity attributes (falling back to 5 credentials-per-user and 10 users on read failure), initializes BoltLockMgr with those limits, then loads persisted state from NVM.
Returns CHIP_NO_ERROR on success; the error from BoltLockMgr().Init() if lock-parameter validation fails; CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND if loading persisted config fails.

**calls** `Init`, `ReadConfigValues`
