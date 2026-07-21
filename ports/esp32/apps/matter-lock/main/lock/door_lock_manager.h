// Door lock manager for the Matter DoorLock cluster: owns bolt lock state plus the users,
// credentials, and weekday/yearday/holiday schedules backing the cluster's server attributes.
// Declares BoltLockManager (accessed via the BoltLockMgr() singleton) and the
// LockInitParams::LockParam/ParamBuilder types used to configure it from zap-derived capacity
// attributes at init time.
/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once
#include <app/clusters/door-lock-server/door-lock-server.h>

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h" // provides FreeRTOS timer support

#include <lib/core/CHIPError.h>

// Pairs a Matter week-day schedule entry with its DlScheduleStatus.
struct WeekDaysScheduleInfo {
    DlScheduleStatus status;
    EmberAfPluginDoorLockWeekDaySchedule schedule;
};

// Pairs a Matter year-day schedule entry with its DlScheduleStatus.
struct YearDayScheduleInfo {
    DlScheduleStatus status;
    EmberAfPluginDoorLockYearDaySchedule schedule;
};

// Holds a Matter door lock holiday schedule entry paired with its DlScheduleStatus.
struct HolidayScheduleInfo {
    DlScheduleStatus status;
    EmberAfPluginDoorLockHolidaySchedule schedule;
};

namespace ESP32DoorLock {
namespace ResourceRanges {
// Used to size arrays
static constexpr uint16_t kMaxUsers                  = 10;
static constexpr uint8_t kMaxCredentialsPerUser      = 10;
static constexpr uint8_t kMaxWeekdaySchedulesPerUser = 10;
static constexpr uint8_t kMaxYeardaySchedulesPerUser = 10;
static constexpr uint8_t kMaxHolidaySchedules        = 10;
static constexpr uint8_t kMaxCredentialSize          = 65;

static constexpr uint8_t kMaxCredentials = kMaxUsers * kMaxCredentialsPerUser;
} // namespace ResourceRanges

// Lock initialization parameters and a fluent builder for constructing them from Matter zap
// attribute values (user, credential, and schedule capacity limits) before passing them to
// BoltLockManager::Init.
namespace LockInitParams {

// Runtime configuration for the door lock, populated from Matter zap attributes: user,
// credential, and schedule capacity limits.
struct LockParam {
    // Read from zap attributes
    uint16_t numberOfUsers                  = 0;
    uint8_t numberOfCredentialsPerUser      = 0;
    uint8_t numberOfWeekdaySchedulesPerUser = 0;
    uint8_t numberOfYeardaySchedulesPerUser = 0;
    uint8_t numberOfHolidaySchedules        = 0;
};

class ParamBuilder {
public:
    ParamBuilder  &SetNumberOfUsers(uint16_t numberOfUsers)
    {
        lockParam_.numberOfUsers = numberOfUsers;
        return *this;
    }
    ParamBuilder  &SetNumberOfCredentialsPerUser(uint8_t numberOfCredentialsPerUser)
    {
        lockParam_.numberOfCredentialsPerUser = numberOfCredentialsPerUser;
        return *this;
    }
    ParamBuilder  &SetNumberOfWeekdaySchedulesPerUser(uint8_t numberOfWeekdaySchedulesPerUser)
    {
        lockParam_.numberOfWeekdaySchedulesPerUser = numberOfWeekdaySchedulesPerUser;
        return *this;
    }
    ParamBuilder  &SetNumberOfYeardaySchedulesPerUser(uint8_t numberOfYeardaySchedulesPerUser)
    {
        lockParam_.numberOfYeardaySchedulesPerUser = numberOfYeardaySchedulesPerUser;
        return *this;
    }
    ParamBuilder  &SetNumberOfHolidaySchedules(uint8_t numberOfHolidaySchedules)
    {
        lockParam_.numberOfHolidaySchedules = numberOfHolidaySchedules;
        return *this;
    }
    // Returns the lock's current LockParam.
    LockParam GetLockParam()
    {
        return lockParam_;
    }

private:
    LockParam lockParam_;
};

} // namespace LockInitParams
} // namespace ESP32DoorLock

using namespace chip;
using namespace ESP32DoorLock::ResourceRanges;

// Manages the door lock's bolt state, users, credentials, and schedules for the Matter
// DoorLock cluster.
// Owns fixed-size in-memory tables (users, credentials, weekday/yearday/holiday schedules)
// sized by kMax* constants, plus the associated name and credential-data buffers. Accessed
// through the process-wide singleton returned by BoltLockMgr(); callers must call Init() (and
// InitLockState()/ReadConfigValues() as needed) before use. Index-validity and
// credential-storage-index helpers must be used to translate cluster indices before touching
// the internal tables.
class BoltLockManager {
public:
    enum Action_t {
        LOCK_ACTION = 0,
        UNLOCK_ACTION,

        INVALID_ACTION
    } Action;

    CHIP_ERROR InitLockState();
    CHIP_ERROR Init(chip::app::DataModel::Nullable<chip::app::Clusters::DoorLock::DlLockState> state,
                    ESP32DoorLock::LockInitParams::LockParam lockParam);

    bool ValidatePIN(chip::EndpointId endpointId, const Optional<chip::ByteSpan>  &pin, OperationErrorEnum  &err) const;

    void Lock(chip::EndpointId endpointId, app::Clusters::DoorLock::OperationSourceEnum source,
              const chip::app::DataModel::Nullable<uint16_t>  &userIndex = chip::app::DataModel::NullNullable);
    void Unlock(chip::EndpointId endpointId, app::Clusters::DoorLock::OperationSourceEnum source,
                const chip::app::DataModel::Nullable<uint16_t>  &userIndex = chip::app::DataModel::NullNullable);

    uint16_t UserIndexForAliroCredential(const chip::ByteSpan  &credentialData);

    bool GetUser(chip::EndpointId endpointId, uint16_t userIndex, EmberAfPluginDoorLockUserInfo  &user);
    bool SetUser(chip::EndpointId endpointId, uint16_t userIndex, chip::FabricIndex creator, chip::FabricIndex modifier,
                 const chip::CharSpan  &userName, uint32_t uniqueId, UserStatusEnum userStatus, UserTypeEnum usertype,
                 CredentialRuleEnum credentialRule, const CredentialStruct * credentials, size_t totalCredentials);

    bool GetCredential(chip::EndpointId endpointId, uint16_t credentialIndex, CredentialTypeEnum credentialType,
                       EmberAfPluginDoorLockCredentialInfo  &credential);

    bool SetCredential(chip::EndpointId endpointId, uint16_t credentialIndex, chip::FabricIndex creator, chip::FabricIndex modifier,
                       DlCredentialStatus credentialStatus, CredentialTypeEnum credentialType,
                       const chip::ByteSpan  &credentialData);

    DlStatus GetWeekdaySchedule(chip::EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex,
                                EmberAfPluginDoorLockWeekDaySchedule  &schedule);

    DlStatus SetWeekdaySchedule(chip::EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex, DlScheduleStatus status,
                                DaysMaskMap daysMask, uint8_t startHour, uint8_t startMinute, uint8_t endHour, uint8_t endMinute);

    DlStatus GetYeardaySchedule(chip::EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex,
                                EmberAfPluginDoorLockYearDaySchedule  &schedule);

    DlStatus SetYeardaySchedule(chip::EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex, DlScheduleStatus status,
                                uint32_t localStartTime, uint32_t localEndTime);

    DlStatus GetHolidaySchedule(chip::EndpointId endpointId, uint8_t holidayIndex, EmberAfPluginDoorLockHolidaySchedule  &schedule);

    DlStatus SetHolidaySchedule(chip::EndpointId endpointId, uint8_t holidayIndex, DlScheduleStatus status, uint32_t localStartTime,
                                uint32_t localEndTime, OperatingModeEnum operatingMode);

    bool IsValidUserIndex(uint16_t userIndex);
    bool IsValidCredentialIndex(uint16_t credentialIndex, CredentialTypeEnum type);
    uint16_t CredentialStorageIndex(uint16_t credentialIndex, CredentialTypeEnum type) const;
    bool IsValidWeekdayScheduleIndex(uint8_t scheduleIndex);
    bool IsValidYeardayScheduleIndex(uint8_t scheduleIndex);
    bool IsValidHolidayScheduleIndex(uint8_t scheduleIndex);
    const char * lockStateToString(DlLockState lockState) const;

    bool ReadConfigValues();

private:
    friend BoltLockManager  &BoltLockMgr();

    EmberAfPluginDoorLockUserInfo mLockUsers[kMaxUsers];
    EmberAfPluginDoorLockCredentialInfo mLockCredentials[kMaxCredentials];
    WeekDaysScheduleInfo mWeekdaySchedule[kMaxUsers][kMaxWeekdaySchedulesPerUser];
    YearDayScheduleInfo mYeardaySchedule[kMaxUsers][kMaxYeardaySchedulesPerUser];
    HolidayScheduleInfo mHolidaySchedule[kMaxHolidaySchedules];

    char mUserNames[MATTER_ARRAY_SIZE(mLockUsers)][DOOR_LOCK_MAX_USER_NAME_SIZE];
    uint8_t mCredentialData[kMaxCredentials][kMaxCredentialSize];
    CredentialStruct mCredentials[kMaxUsers][kMaxCredentialsPerUser];

    static BoltLockManager sLock;
    ESP32DoorLock::LockInitParams::LockParam LockParams;
};

// Returns the process-wide BoltLockManager singleton (BoltLockManager::sLock).
inline BoltLockManager  &BoltLockMgr()
{
    return BoltLockManager::sLock;
}
