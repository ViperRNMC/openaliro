<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h`

Door lock manager for the Matter DoorLock cluster: owns bolt lock state plus the users,
credentials, and weekday/yearday/holiday schedules backing the cluster's server attributes.
Declares BoltLockManager (accessed via the BoltLockMgr() singleton) and the
LockInitParams::LockParam/ParamBuilder types used to configure it from zap-derived capacity
attributes at init time.

**used by** [`ports/esp32/apps/matter-lock/main/app_main.cpp`](../ports.esp32.apps.matter-lock.main/app_main.cpp.md), [`ports/esp32/apps/matter-lock/main/app_shell.cpp`](../ports.esp32.apps.matter-lock.main/app_shell.cpp.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_callbacks.cpp`](door_lock_callbacks.cpp.md), [`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.cpp`](door_lock_manager.cpp.md)

## API

### `struct WeekDaysScheduleInfo`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h:26`

Pairs a Matter week-day schedule entry with its DlScheduleStatus.

### `struct YearDayScheduleInfo`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h:32`

Pairs a Matter year-day schedule entry with its DlScheduleStatus.

### `struct HolidayScheduleInfo`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h:38`

Holds a Matter door lock holiday schedule entry paired with its DlScheduleStatus.

### `namespace LockInitParams`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h:59`

Lock initialization parameters and a fluent builder for constructing them from Matter zap
attribute values (user, credential, and schedule capacity limits) before passing them to
BoltLockManager::Init.

### `struct LockParam`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h:63`

Runtime configuration for the door lock, populated from Matter zap attributes: user,
credential, and schedule capacity limits.

### `LockParam GetLockParam()`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h:100`

Returns the lock's current LockParam.

### `class BoltLockManager`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h:123`

Manages the door lock's bolt state, users, credentials, and schedules for the Matter
DoorLock cluster.
Owns fixed-size in-memory tables (users, credentials, weekday/yearday/holiday schedules)
sized by kMax* constants, plus the associated name and credential-data buffers. Accessed
through the process-wide singleton returned by BoltLockMgr(); callers must call Init() (and
InitLockState()/ReadConfigValues() as needed) before use. Index-validity and
credential-storage-index helpers must be used to translate cluster indices before touching
the internal tables.

**calls** `BoltLockMgr`

### `inline BoltLockManager &BoltLockMgr()`
`ports/esp32/apps/matter-lock/main/lock/door_lock_manager.h:202`

Returns the process-wide BoltLockManager singleton (BoltLockManager::sLock).

**called by** `class`
