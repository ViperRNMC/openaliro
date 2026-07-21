// BoltLockManager: Matter door lock cluster backing store for the ESP32 port. Implements the DoorLock cluster's user, credential, and weekday/yearday/holiday schedule get/set callbacks over fixed-size in-memory tables mirrored to NVM (ESP32Config blobs), plus lock/unlock actuation and PIN validation. Cluster indices are one-indexed by Matter and decremented internally before bounds-checking against this platform's fixed capacity limits.
/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <door_lock_manager.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/CHIPDeviceError.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <cstring>
#include <esp_log.h>

/* Defined in app_driver.cpp. Declared here rather than including app_priv.h,
 * whose esp_matter includes make this file's TAG ambiguous. */
void app_driver_led_lock_state(bool locked, bool aliro);

static const char *TAG = "doorlock_manager";

BoltLockManager BoltLockManager::sLock;

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::DeviceLayer::Internal;
using namespace ESP32DoorLock::LockInitParams;
using namespace chip::Protocols::InteractionModel;
namespace {

// Read an optional fixed-size config blob into data. Treats "config not found" as success (the blob
// is optional and data is left untouched), logs and returns false on any other read error.
//
// A size mismatch means the blob was written by a build whose record layout differs from this one
// (for example EmberAfPluginDoorLockUserInfo growing across a CHIP update). That is a stale on-disk
// format, not a read failure: the partially-filled buffer is unusable, so *outStale is set and the
// caller reinitialises and rewrites every table. Reporting it as a failure instead would fail
// InitLockState() on every boot with no path back to a good state.
bool ReadOptionalConfigBlob(ESP32Config::Key key, uint8_t *data, size_t dataLen, bool *outStale)
{
    size_t outLen = 0;
    CHIP_ERROR err = ESP32Config::ReadConfigValueBin(key, data, dataLen, outLen);
    if (err == CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND) {
        return true;
    }
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Failed to read door lock config blob '%s': %s", key.Name, err.AsString());
        return false;
    }
    if (outLen != dataLen) {
        ESP_LOGW(TAG, "Door lock config blob '%s' has stale size %u, expected %u; reinitialising", key.Name,
                 static_cast<unsigned>(outLen), static_cast<unsigned>(dataLen));
        *outStale = true;
        return true;
    }
    return true;
}
// Write a fixed-size config blob to persistent storage. Logs and returns false on write failure.
bool WriteConfigBlob(ESP32Config::Key key, const uint8_t *data, size_t dataLen)
{
    CHIP_ERROR err = ESP32Config::WriteConfigValueBin(key, data, dataLen);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to write door lock config blob '%s': %s", key.Name, err.AsString());
        return false;
    }
    return true;
}

} // namespace

// Validate the configured lock parameters against this platform's fixed capacity limits
// (users, credentials per user, weekday/yearday schedules per user, holiday schedules) and store
// them. Returns CHIP_ERROR_NO_MEMORY if any configured count exceeds its platform maximum, otherwise
// CHIP_NO_ERROR. The state parameter is accepted but unused.
CHIP_ERROR BoltLockManager::Init(DataModel::Nullable<DoorLock::DlLockState> state,
                                 LockParam lockParam)
{
    LockParams = lockParam;

    if (LockParams.numberOfUsers > kMaxUsers) {
        ESP_LOGI(TAG, "Max number of users is greater than %d, the maximum amount of users currently supported on this platform", kMaxUsers);
        return CHIP_ERROR_NO_MEMORY;
    }

    if (LockParams.numberOfCredentialsPerUser > kMaxCredentialsPerUser) {
        ESP_LOGI(TAG, "Max number of credentials per user is greater than %d, the maximum amount of users currently supported on "
                 "this platform", kMaxCredentialsPerUser);
        return CHIP_ERROR_NO_MEMORY;
    }

    if (LockParams.numberOfWeekdaySchedulesPerUser > kMaxWeekdaySchedulesPerUser) {
        ESP_LOGI(TAG, " Max number of schedules is greater than %d, the maximum amount of schedules currently supported on this platform",
                 kMaxWeekdaySchedulesPerUser);
        return CHIP_ERROR_NO_MEMORY;
    }

    if (LockParams.numberOfYeardaySchedulesPerUser > kMaxYeardaySchedulesPerUser) {
        ESP_LOGI(TAG, "Max number of schedules is greater than %d, the maximum amount of schedules currently supported on this platform",
                 kMaxYeardaySchedulesPerUser);
        return CHIP_ERROR_NO_MEMORY;
    }

    if (LockParams.numberOfHolidaySchedules > kMaxHolidaySchedules) {
        ESP_LOGI(TAG, "Max number of schedules is greater than %d, the maximum amount of schedules currently supported on this platform",
                 kMaxHolidaySchedules);
        return CHIP_ERROR_NO_MEMORY;
    }

    return CHIP_NO_ERROR;
}

// Check whether userIndex is a valid user index (less than kMaxUsers).
bool BoltLockManager::IsValidUserIndex(uint16_t userIndex)
{
    return (userIndex < kMaxUsers);
}

// Check whether credentialIndex is valid for the given credential type. Programming PIN credentials
// require index 0; all other types require an index less than kMaxCredentialsPerUser.
bool BoltLockManager::IsValidCredentialIndex(uint16_t credentialIndex, CredentialTypeEnum type)
{
    if (CredentialTypeEnum::kProgrammingPIN == type) {
        return (0 == credentialIndex); // 0 is required index for Programming PIN
    }
    return (credentialIndex < kMaxCredentialsPerUser);
}

// Compute the flat storage index for a credential of the given type and per-user index, packing
// type and index into a single slot number (type * kMaxCredentialsPerUser + credentialIndex).
uint16_t BoltLockManager::CredentialStorageIndex(uint16_t credentialIndex, CredentialTypeEnum type) const
{
    return static_cast<uint16_t>(to_underlying(type) * kMaxCredentialsPerUser + credentialIndex);
}

// Check whether scheduleIndex is a valid weekday schedule index (less than
// kMaxWeekdaySchedulesPerUser).
bool BoltLockManager::IsValidWeekdayScheduleIndex(uint8_t scheduleIndex)
{
    return (scheduleIndex < kMaxWeekdaySchedulesPerUser);
}

// Check whether scheduleIndex is a valid yearday schedule index (less than
// kMaxYeardaySchedulesPerUser).
bool BoltLockManager::IsValidYeardayScheduleIndex(uint8_t scheduleIndex)
{
    return (scheduleIndex < kMaxYeardaySchedulesPerUser);
}

// Check whether scheduleIndex is a valid holiday schedule index (less than kMaxHolidaySchedules).
bool BoltLockManager::IsValidHolidayScheduleIndex(uint8_t scheduleIndex)
{
    return (scheduleIndex < kMaxHolidaySchedules);
}

// Loads all persisted door-lock state (users, credentials, names, credential data, per-user credential links, weekday/yearday/holiday schedules) from NVM into the in-memory tables.
// Detects a stale database, either by a blob whose stored size does not match this build's table
// size or by credential slot 0 holding a non-programming-PIN type (an old mixed-type layout); in
// either case clears every in-memory table and rewrites all blobs to NVM.
// Returns true if all blob reads (and, if triggered, all stale-format rewrites) succeed; false if any read or rewrite fails.
//
// Every blob is sized by its own array via sizeof, never by the runtime LockParams counts. The
// counts come from Matter attributes and can differ from the compiled capacity, which would make
// the read length disagree with the write length and re-flag the blob as stale on every boot.
bool BoltLockManager::ReadConfigValues()
{
    bool ok = true;
    bool stale = false;
    ok &= ReadOptionalConfigBlob(ESP32Config::kConfigKey_LockUser, reinterpret_cast<uint8_t *>(mLockUsers),
                                 sizeof(mLockUsers), &stale);
    ok &= ReadOptionalConfigBlob(ESP32Config::kConfigKey_Credential, reinterpret_cast<uint8_t *>(mLockCredentials),
                                 sizeof(mLockCredentials), &stale);
    ok &= ReadOptionalConfigBlob(ESP32Config::kConfigKey_LockUserName, reinterpret_cast<uint8_t *>(mUserNames),
                                 sizeof(mUserNames), &stale);
    ok &= ReadOptionalConfigBlob(ESP32Config::kConfigKey_CredentialData, reinterpret_cast<uint8_t *>(mCredentialData),
                                 sizeof(mCredentialData), &stale);
    ok &= ReadOptionalConfigBlob(ESP32Config::kConfigKey_UserCredentials, reinterpret_cast<uint8_t *>(mCredentials),
                                 sizeof(mCredentials), &stale);
    ok &= ReadOptionalConfigBlob(ESP32Config::kConfigKey_WeekDaySchedules, reinterpret_cast<uint8_t *>(mWeekdaySchedule),
                                 sizeof(mWeekdaySchedule), &stale);
    ok &= ReadOptionalConfigBlob(ESP32Config::kConfigKey_YearDaySchedules, reinterpret_cast<uint8_t *>(mYeardaySchedule),
                                 sizeof(mYeardaySchedule), &stale);
    ok &= ReadOptionalConfigBlob(ESP32Config::kConfigKey_HolidaySchedules, reinterpret_cast<uint8_t *>(mHolidaySchedule),
                                 sizeof(mHolidaySchedule), &stale);
    if (!ok) {
        return false;
    }
    if (stale || (mLockCredentials[0].status == DlCredentialStatus::kOccupied &&
                  mLockCredentials[0].credentialType != CredentialTypeEnum::kProgrammingPIN)) {
        ESP_LOGW(TAG, "Clearing stale door lock database");
        for (auto &user : mLockUsers) {
            user = EmberAfPluginDoorLockUserInfo();
        }
        for (auto &credential : mLockCredentials) {
            credential = EmberAfPluginDoorLockCredentialInfo();
        }
        memset(mUserNames, 0, sizeof(mUserNames));
        memset(mCredentialData, 0, sizeof(mCredentialData));
        memset(mCredentials, 0, sizeof(mCredentials));
        /* A stale size can belong to any blob, including the schedule tables, so clear and rewrite
         * those too rather than leaving a partially-read buffer behind. */
        memset(mWeekdaySchedule, 0, sizeof(mWeekdaySchedule));
        memset(mYeardaySchedule, 0, sizeof(mYeardaySchedule));
        memset(mHolidaySchedule, 0, sizeof(mHolidaySchedule));
        ok &= WriteConfigBlob(ESP32Config::kConfigKey_LockUser, reinterpret_cast<const uint8_t *>(&mLockUsers),
                              sizeof(mLockUsers));
        ok &= WriteConfigBlob(ESP32Config::kConfigKey_Credential, reinterpret_cast<const uint8_t *>(&mLockCredentials),
                              sizeof(mLockCredentials));
        ok &= WriteConfigBlob(ESP32Config::kConfigKey_LockUserName, reinterpret_cast<const uint8_t *>(mUserNames),
                              sizeof(mUserNames));
        ok &= WriteConfigBlob(ESP32Config::kConfigKey_CredentialData, reinterpret_cast<const uint8_t *>(&mCredentialData),
                              sizeof(mCredentialData));
        ok &= WriteConfigBlob(ESP32Config::kConfigKey_UserCredentials, reinterpret_cast<const uint8_t *>(mCredentials),
                              sizeof(mCredentials));
        ok &= WriteConfigBlob(ESP32Config::kConfigKey_WeekDaySchedules, reinterpret_cast<const uint8_t *>(mWeekdaySchedule),
                              sizeof(mWeekdaySchedule));
        ok &= WriteConfigBlob(ESP32Config::kConfigKey_YearDaySchedules, reinterpret_cast<const uint8_t *>(mYeardaySchedule),
                              sizeof(mYeardaySchedule));
        ok &= WriteConfigBlob(ESP32Config::kConfigKey_HolidaySchedules, reinterpret_cast<const uint8_t *>(mHolidaySchedule),
                              sizeof(mHolidaySchedule));
    }
    return ok;
}

// Lock the bolt on the given endpoint, updating the Matter door lock cluster state and the status
// LED. Records source as the cause of the lock transition.
// userIndex: one-indexed user the operation is attributed to in the LockOperation event, or null
// when the operation cannot be traced to a stored user.
void BoltLockManager::Lock(EndpointId endpointId, OperationSourceEnum source, const DataModel::Nullable<uint16_t>  &userIndex)
{
    DoorLockServer::Instance().SetLockState(endpointId, DlLockState::kLocked, source, userIndex);
    app_driver_led_lock_state(true, false);
}

// Unlock the bolt on the given endpoint, updating the Matter door lock cluster state and the status
// LED. Drives the LED's Aliro-specific indication when source is kAliro.
// userIndex: one-indexed user the operation is attributed to in the LockOperation event, or null
// when the operation cannot be traced to a stored user.
void BoltLockManager::Unlock(EndpointId endpointId, OperationSourceEnum source, const DataModel::Nullable<uint16_t>  &userIndex)
{
    DoorLockServer::Instance().SetLockState(endpointId, DlLockState::kUnlocked, source, userIndex);
    app_driver_led_lock_state(false, source == OperationSourceEnum::kAliro);
}

// Find the user that owns a given Aliro endpoint key, so an approach unlock can be attributed to a
// person in the LockOperation event rather than reported anonymously.
// credentialData: the credential public key the reader authenticated (uncompressed P-256, 65 bytes).
// Scans occupied users' credential links for an Aliro endpoint-key credential whose stored bytes
// match, comparing against the mCredentialData backing store directly (the ByteSpan in
// mLockCredentials is restored from NVM with a stale pointer after a reboot).
// Returns the one-indexed user index, or 0 if no occupied user owns a matching credential.
uint16_t BoltLockManager::UserIndexForAliroCredential(const ByteSpan  &credentialData)
{
    for (uint16_t userIndex = 0; userIndex < kMaxUsers; userIndex++) {
        if (UserStatusEnum::kAvailable == mLockUsers[userIndex].userStatus) {
            continue;
        }
        for (uint8_t i = 0; i < kMaxCredentialsPerUser; i++) {
            const auto  &link = mCredentials[userIndex][i];
            if (link.credentialType != CredentialTypeEnum::kAliroEvictableEndpointKey &&
                    link.credentialType != CredentialTypeEnum::kAliroNonEvictableEndpointKey) {
                continue;
            }
            // Credential links carry the one-indexed cluster index; the storage index is
            // computed from the zero-based one, as in GetCredential.
            if (link.credentialIndex == 0 || !IsValidCredentialIndex(link.credentialIndex - 1, link.credentialType)) {
                continue;
            }
            uint16_t storageIndex = CredentialStorageIndex(link.credentialIndex - 1, link.credentialType);
            if (DlCredentialStatus::kOccupied != mLockCredentials[storageIndex].status) {
                continue;
            }
            if (credentialData.data_equal(ByteSpan(mCredentialData[storageIndex], kMaxCredentialSize))) {
                return static_cast<uint16_t>(userIndex + 1); // user indices are one-indexed
            }
        }
    }
    return 0;
}

// Looks up a stored user by index for the Matter door lock cluster.
// userIndex: one-indexed by the caller; decremented here and validated against kMaxUsers.
// user: output; on an available (unoccupied) slot only userStatus is set; on an occupied slot name, credentials, unique ID, type, credential rule, and creator/modifier are also filled in, with creation/modification source always reported as Matter.
// Returns true if userIndex is valid, whether or not the slot is occupied; returns false only if userIndex (0 or out of range) is invalid.
bool BoltLockManager::GetUser(EndpointId endpointId, uint16_t userIndex, EmberAfPluginDoorLockUserInfo  &user)
{
    VerifyOrReturnValue(userIndex > 0, false); // indices are one-indexed

    userIndex--;

    VerifyOrReturnValue(IsValidUserIndex(userIndex), false);

    ESP_LOGI(TAG, "Door Lock App: BoltLockManager::GetUser [endpoint=%d,userIndex=%hu]", endpointId, userIndex);

    const auto  &userInDb = mLockUsers[userIndex];

    user.userStatus = userInDb.userStatus;
    if (UserStatusEnum::kAvailable == user.userStatus) {
        ESP_LOGI(TAG, "Found unoccupied user [endpoint=%d]", endpointId);
        return true;
    }

    user.userName       = CharSpan(userInDb.userName.data(), userInDb.userName.size());
    user.credentials    = Span<const CredentialStruct>(mCredentials[userIndex], userInDb.credentials.size());
    user.userUniqueId   = userInDb.userUniqueId;
    user.userType       = userInDb.userType;
    user.credentialRule = userInDb.credentialRule;
    // So far there's no way to actually create the credential outside Matter, so here we always set the creation/modification
    // source to Matter
    user.creationSource     = DlAssetSource::kMatterIM;
    user.createdBy          = userInDb.createdBy;
    user.modificationSource = DlAssetSource::kMatterIM;
    user.lastModifiedBy     = userInDb.lastModifiedBy;

    ESP_LOGI(TAG, "Found occupied user [endpoint=%d,name=\"%.*s\",credentialsCount=%u,uniqueId=%" PRIu32
             ",type=%u,credentialRule=%u,createdBy=%d,lastModifiedBy=%d]",
             endpointId, static_cast<int>(user.userName.size()), user.userName.data(), user.credentials.size(), user.userUniqueId,
             to_underlying(user.userType), to_underlying(user.credentialRule), user.createdBy, user.lastModifiedBy);

    return true;
}

// Stores a user record by index for the Matter door lock cluster, persisting it and its credential links to NVM.
// userIndex: one-indexed by the caller; decremented here and validated against kMaxUsers.
// creator, modifier: fabric indices recorded as the user's creator and last modifier.
// userName: must not exceed DOOR_LOCK_MAX_USER_NAME_SIZE.
// credentials, totalCredentials: credential list to associate with the user; totalCredentials must not exceed LockParams.numberOfCredentialsPerUser.
// Returns false if userIndex is invalid, userName is too long, totalCredentials is too large, or the NVM write fails; true on success.
bool BoltLockManager::SetUser(EndpointId endpointId, uint16_t userIndex, FabricIndex creator,
                              FabricIndex modifier, const CharSpan  &userName, uint32_t uniqueId,
                              UserStatusEnum userStatus, UserTypeEnum usertype, CredentialRuleEnum credentialRule,
                              const CredentialStruct * credentials, size_t totalCredentials)
{
    ESP_LOGI(TAG, "Door Lock App: BoltLockManager::SetUser "
             "[endpoint=%d,userIndex=%d,creator=%d,modifier=%d,userName=%s,uniqueId=%" PRIu32 ""
             "userStatus=%u,userType=%u,credentialRule=%u,credentials=%p,totalCredentials=%u]",
             endpointId, userIndex, creator, modifier, userName.data(), uniqueId, to_underlying(userStatus),
             to_underlying(usertype), to_underlying(credentialRule), credentials, totalCredentials);

    VerifyOrReturnValue(userIndex > 0, false); // indices are one-indexed

    userIndex--;

    VerifyOrReturnValue(IsValidUserIndex(userIndex), false);

    auto  &userInStorage = mLockUsers[userIndex];

    if (userName.size() > DOOR_LOCK_MAX_USER_NAME_SIZE) {
        ESP_LOGE(TAG, "Cannot set user - user name is too long [endpoint=%d,index=%d]", endpointId, userIndex);
        return false;
    }

    if (totalCredentials > LockParams.numberOfCredentialsPerUser) {
        ESP_LOGE(TAG, "Cannot set user - total number of credentials is too big [endpoint=%d,index=%d,totalCredentials=%u]",
                 endpointId, userIndex, totalCredentials);
        return false;
    }

    Platform::CopyString(mUserNames[userIndex], userName);
    userInStorage.userName       = CharSpan(mUserNames[userIndex], userName.size());
    userInStorage.userUniqueId   = uniqueId;
    userInStorage.userStatus     = userStatus;
    userInStorage.userType       = usertype;
    userInStorage.credentialRule = credentialRule;
    userInStorage.lastModifiedBy = modifier;
    userInStorage.createdBy      = creator;

    for (size_t i = 0; i < totalCredentials; ++i) {
        mCredentials[userIndex][i] = credentials[i];
    }
    userInStorage.credentials = Span<const CredentialStruct>(mCredentials[userIndex], totalCredentials);

    // Save user information in NVM flash
    if (!WriteConfigBlob(ESP32Config::kConfigKey_LockUser, reinterpret_cast<const uint8_t *>(mLockUsers),
                         sizeof(mLockUsers)) ||
            !WriteConfigBlob(ESP32Config::kConfigKey_UserCredentials, reinterpret_cast<const uint8_t *>(mCredentials),
                             sizeof(mCredentials)) ||
            !WriteConfigBlob(ESP32Config::kConfigKey_LockUserName, reinterpret_cast<const uint8_t *>(mUserNames),
                             sizeof(mUserNames))) {
        return false;
    }
    ESP_LOGI(TAG, "Successfully set the user [mEndpointId=%d,index=%d]", endpointId, userIndex);

    return true;
}

// Looks up a stored credential by index and type for the Matter door lock cluster.
// Programming-PIN indices are used as-is; all other credential types are one-indexed by the caller and decremented here.
// endpointId: unused, present for cluster callback signature compatibility.
// credentialIndex: caller-supplied index; must pass IsValidCredentialIndex after the type-specific decrement.
// credentialType: type of credential being looked up.
// credential: output; on an available (unoccupied) slot only status is set; on an occupied slot type, data, and creator/modifier are also filled in, with creation/modification source always reported as Matter.
// Returns true if credentialIndex is valid, whether or not the slot is occupied; returns false only if credentialIndex is invalid for the given type.
bool BoltLockManager::GetCredential(EndpointId endpointId, uint16_t credentialIndex, CredentialTypeEnum credentialType,
                                    EmberAfPluginDoorLockCredentialInfo  &credential)
{
    if (CredentialTypeEnum::kProgrammingPIN == credentialType) {
        VerifyOrReturnValue(IsValidCredentialIndex(credentialIndex, credentialType),
                            false); // programming pin index is only index allowed to contain 0
    } else {
        VerifyOrReturnValue(IsValidCredentialIndex(--credentialIndex, credentialType), false); // otherwise, indices are one-indexed
    }

    ESP_LOGI(TAG, "Lock App: BoltLockManager::GetCredential [credentialType=%u], credentialIndex=%d", to_underlying(credentialType),
             credentialIndex);
    uint16_t storageIndex = CredentialStorageIndex(credentialIndex, credentialType);
    const auto  &credentialInStorage = mLockCredentials[storageIndex];
    credential.status = credentialInStorage.status;
    ESP_LOGI(TAG, "CredentialStatus: %d, CredentialIndex: %d, StorageIndex: %d ", (int) credential.status,
             credentialIndex, storageIndex);
    if (DlCredentialStatus::kAvailable == credential.status) {
        ESP_LOGI(TAG, "Found unoccupied credential ");
        return true;
    }
    credential.credentialType = credentialInStorage.credentialType;
    credential.credentialData = credentialInStorage.credentialData;
    credential.createdBy      = credentialInStorage.createdBy;
    credential.lastModifiedBy = credentialInStorage.lastModifiedBy;
    // So far there's no way to actually create the credential outside Matter, so here we always set the creation/modification
    // source to Matter
    credential.creationSource     = DlAssetSource::kMatterIM;
    credential.modificationSource = DlAssetSource::kMatterIM;

    ESP_LOGI(TAG, "Found occupied credential [type=%u,dataSize=%u]", to_underlying(credential.credentialType),
             credential.credentialData.size());

    return true;
}

// Stores a credential by index and type for the Matter door lock cluster, persisting it to NVM.
// Programming-PIN indices are used as-is; all other credential types are one-indexed by the caller and decremented here.
// endpointId: unused, present for cluster callback signature compatibility.
// credentialIndex: caller-supplied index; must pass IsValidCredentialIndex after the type-specific decrement.
// creator, modifier: fabric indices recorded as the credential's creator and last modifier.
// credentialStatus: status to store for this credential slot.
// credentialType: type of credential being stored.
// credentialData: raw credential bytes; must not exceed kMaxCredentialSize.
// Returns true on success; false if credentialIndex is invalid, credentialData exceeds kMaxCredentialSize, or the NVM write fails.
bool BoltLockManager::SetCredential(EndpointId endpointId, uint16_t credentialIndex, FabricIndex creator,
                                    FabricIndex modifier, DlCredentialStatus credentialStatus,
                                    CredentialTypeEnum credentialType, const ByteSpan  &credentialData)
{
    if (CredentialTypeEnum::kProgrammingPIN == credentialType) {
        VerifyOrReturnValue(IsValidCredentialIndex(credentialIndex, credentialType),
                            false); // programming pin index is only index allowed to contain 0
    } else {
        VerifyOrReturnValue(IsValidCredentialIndex(--credentialIndex, credentialType), false); // otherwise, indices are one-indexed
    }

    ESP_LOGI(TAG, "Door Lock App: BoltLockManager::SetCredential "
             "[credentialStatus=%u,credentialType=%u,credentialDataSize=%u,creator=%d,modifier=%d]",
             to_underlying(credentialStatus), to_underlying(credentialType), credentialData.size(), creator, modifier);

    VerifyOrReturnValue(credentialData.size() <= kMaxCredentialSize, false);
    uint16_t storageIndex = CredentialStorageIndex(credentialIndex, credentialType);
    auto  &credentialInStorage = mLockCredentials[storageIndex];
    credentialInStorage.status         = credentialStatus;
    credentialInStorage.credentialType = credentialType;
    credentialInStorage.createdBy      = creator;
    credentialInStorage.lastModifiedBy = modifier;
    memcpy(mCredentialData[storageIndex], credentialData.data(), credentialData.size());
    credentialInStorage.credentialData = ByteSpan{ mCredentialData[storageIndex], credentialData.size() };
    // Save credential information in NVM flash
    if (!WriteConfigBlob(ESP32Config::kConfigKey_Credential, reinterpret_cast<const uint8_t *>(&mLockCredentials),
                         sizeof(mLockCredentials)) ||
            !WriteConfigBlob(ESP32Config::kConfigKey_CredentialData, reinterpret_cast<const uint8_t *>(&mCredentialData),
                             sizeof(mCredentialData))) {
        return false;
    }
    ESP_LOGI(TAG, "Successfully set the credential [credentialType=%u]", to_underlying(credentialType));

    return true;
}

// Looks up a stored weekday schedule by weekday index and user index for the Matter door lock cluster.
// weekdayIndex, userIndex: both one-indexed by the caller; decremented here and validated against their respective max counts.
// schedule: output, populated only when the slot is occupied.
// Returns DlStatus::kFailure if either index is 0 or out of range, kNotFound if the slot is unoccupied, kSuccess otherwise.
DlStatus BoltLockManager::GetWeekdaySchedule(EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex,
                                             EmberAfPluginDoorLockWeekDaySchedule  &schedule)
{
    VerifyOrReturnValue(weekdayIndex > 0, DlStatus::kFailure); // indices are one-indexed
    VerifyOrReturnValue(userIndex > 0, DlStatus::kFailure);    // indices are one-indexed

    weekdayIndex--;
    userIndex--;

    VerifyOrReturnValue(IsValidWeekdayScheduleIndex(weekdayIndex), DlStatus::kFailure);
    VerifyOrReturnValue(IsValidUserIndex(userIndex), DlStatus::kFailure);

    const auto  &scheduleInStorage = mWeekdaySchedule[userIndex][weekdayIndex];
    if (DlScheduleStatus::kAvailable == scheduleInStorage.status) {
        return DlStatus::kNotFound;
    }

    schedule = scheduleInStorage.schedule;

    return DlStatus::kSuccess;
}

// Stores a weekday schedule by weekday index and user index for the Matter door lock cluster, persisting it to NVM.
// weekdayIndex, userIndex: both one-indexed by the caller; decremented here and validated against their respective max counts.
// status, daysMask, startHour, startMinute, endHour, endMinute: schedule fields written to the slot.
// Returns DlStatus::kFailure if either index is 0, out of range, or the NVM write fails; kSuccess otherwise.
DlStatus BoltLockManager::SetWeekdaySchedule(EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex,
                                             DlScheduleStatus status, DaysMaskMap daysMask, uint8_t startHour, uint8_t startMinute,
                                             uint8_t endHour, uint8_t endMinute)
{
    VerifyOrReturnValue(weekdayIndex > 0, DlStatus::kFailure); // indices are one-indexed
    VerifyOrReturnValue(userIndex > 0, DlStatus::kFailure);    // indices are one-indexed

    weekdayIndex--;
    userIndex--;

    VerifyOrReturnValue(IsValidWeekdayScheduleIndex(weekdayIndex), DlStatus::kFailure);
    VerifyOrReturnValue(IsValidUserIndex(userIndex), DlStatus::kFailure);

    auto  &scheduleInStorage = mWeekdaySchedule[userIndex][weekdayIndex];

    scheduleInStorage.schedule.daysMask    = daysMask;
    scheduleInStorage.schedule.startHour   = startHour;
    scheduleInStorage.schedule.startMinute = startMinute;
    scheduleInStorage.schedule.endHour     = endHour;
    scheduleInStorage.schedule.endMinute   = endMinute;
    scheduleInStorage.status               = status;

    // Save schedule information in NVM flash
    if (!WriteConfigBlob(ESP32Config::kConfigKey_WeekDaySchedules, reinterpret_cast<const uint8_t *>(mWeekdaySchedule),
                         sizeof(mWeekdaySchedule))) {
        return DlStatus::kFailure;
    }
    return DlStatus::kSuccess;
}

// Looks up a stored yearday schedule by yearday index and user index for the Matter door lock cluster.
// yearDayIndex, userIndex: both one-indexed by the caller; decremented here and validated against their respective max counts.
// schedule: output, populated only when the slot is occupied.
// Returns DlStatus::kFailure if either index is 0 or out of range, kNotFound if the slot is unoccupied, kSuccess otherwise.
DlStatus BoltLockManager::GetYeardaySchedule(EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex,
                                             EmberAfPluginDoorLockYearDaySchedule  &schedule)
{
    VerifyOrReturnValue(yearDayIndex > 0, DlStatus::kFailure); // indices are one-indexed
    VerifyOrReturnValue(userIndex > 0, DlStatus::kFailure);    // indices are one-indexed

    yearDayIndex--;
    userIndex--;

    VerifyOrReturnValue(IsValidYeardayScheduleIndex(yearDayIndex), DlStatus::kFailure);
    VerifyOrReturnValue(IsValidUserIndex(userIndex), DlStatus::kFailure);

    const auto  &scheduleInStorage = mYeardaySchedule[userIndex][yearDayIndex];
    if (DlScheduleStatus::kAvailable == scheduleInStorage.status) {
        return DlStatus::kNotFound;
    }

    schedule = scheduleInStorage.schedule;

    return DlStatus::kSuccess;
}

// Stores a yearday schedule by yearday index and user index for the Matter door lock cluster, persisting it to NVM.
// yearDayIndex, userIndex: both one-indexed by the caller; decremented here and validated against their respective max counts.
// status, localStartTime, localEndTime: schedule fields written to the slot.
// Returns DlStatus::kFailure if either index is 0, out of range, or the NVM write fails; kSuccess otherwise.
DlStatus BoltLockManager::SetYeardaySchedule(EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex,
                                             DlScheduleStatus status, uint32_t localStartTime, uint32_t localEndTime)
{
    VerifyOrReturnValue(yearDayIndex > 0, DlStatus::kFailure); // indices are one-indexed
    VerifyOrReturnValue(userIndex > 0, DlStatus::kFailure);    // indices are one-indexed

    yearDayIndex--;
    userIndex--;

    VerifyOrReturnValue(IsValidYeardayScheduleIndex(yearDayIndex), DlStatus::kFailure);
    VerifyOrReturnValue(IsValidUserIndex(userIndex), DlStatus::kFailure);

    auto  &scheduleInStorage = mYeardaySchedule[userIndex][yearDayIndex];

    scheduleInStorage.schedule.localStartTime = localStartTime;
    scheduleInStorage.schedule.localEndTime   = localEndTime;
    scheduleInStorage.status                  = status;

    // Save schedule information in NVM flash
    if (!WriteConfigBlob(ESP32Config::kConfigKey_YearDaySchedules, reinterpret_cast<const uint8_t *>(mYeardaySchedule),
                         sizeof(mYeardaySchedule))) {
        return DlStatus::kFailure;
    }
    return DlStatus::kSuccess;
}

// Looks up a stored holiday schedule by index for the Matter door lock cluster.
// holidayIndex: one-indexed by the caller; decremented here and validated against kMaxHolidaySchedules.
// schedule: output, populated only when the slot is occupied.
// Returns DlStatus::kFailure if holidayIndex is 0 or out of range, kNotFound if the slot is unoccupied, kSuccess otherwise.
DlStatus BoltLockManager::GetHolidaySchedule(EndpointId endpointId, uint8_t holidayIndex,
                                             EmberAfPluginDoorLockHolidaySchedule  &schedule)
{
    VerifyOrReturnValue(holidayIndex > 0, DlStatus::kFailure); // indices are one-indexed

    holidayIndex--;

    VerifyOrReturnValue(IsValidHolidayScheduleIndex(holidayIndex), DlStatus::kFailure);

    const auto  &scheduleInStorage = mHolidaySchedule[holidayIndex];
    if (DlScheduleStatus::kAvailable == scheduleInStorage.status) {
        return DlStatus::kNotFound;
    }

    schedule = scheduleInStorage.schedule;

    return DlStatus::kSuccess;
}

// Stores a holiday schedule by index for the Matter door lock cluster, persisting it to NVM.
// holidayIndex: one-indexed by the caller; decremented here and validated against kMaxHolidaySchedules.
// status, localStartTime, localEndTime, operatingMode: schedule fields written to the slot.
// Returns DlStatus::kFailure if holidayIndex is 0, out of range, or the NVM write fails; kSuccess otherwise.
DlStatus BoltLockManager::SetHolidaySchedule(EndpointId endpointId, uint8_t holidayIndex, DlScheduleStatus status,
                                             uint32_t localStartTime, uint32_t localEndTime, OperatingModeEnum operatingMode)
{
    VerifyOrReturnValue(holidayIndex > 0, DlStatus::kFailure); // indices are one-indexed

    holidayIndex--;

    VerifyOrReturnValue(IsValidHolidayScheduleIndex(holidayIndex), DlStatus::kFailure);

    auto  &scheduleInStorage = mHolidaySchedule[holidayIndex];

    scheduleInStorage.schedule.localStartTime = localStartTime;
    scheduleInStorage.schedule.localEndTime   = localEndTime;
    scheduleInStorage.schedule.operatingMode  = operatingMode;
    scheduleInStorage.status                  = status;

    // Save schedule information in NVM flash
    if (!WriteConfigBlob(ESP32Config::kConfigKey_HolidaySchedules, reinterpret_cast<const uint8_t *>(mHolidaySchedule),
                         sizeof(mHolidaySchedule))) {
        return DlStatus::kFailure;
    }
    return DlStatus::kSuccess;
}

// Return a human-readable string for a DlLockState value ("Not Fully Locked", "Locked", "Unlocked",
// "Unlatched"), or "Unknown" for kUnknownEnumValue or any unrecognized value.
const char * BoltLockManager::lockStateToString(DlLockState lockState) const
{
    switch (lockState) {
    case DlLockState::kNotFullyLocked:
        return "Not Fully Locked";
    case DlLockState::kLocked:
        return "Locked";
    case DlLockState::kUnlocked:
        return "Unlocked";
    case DlLockState::kUnlatched:
        return "Unlatched";
    case DlLockState::kUnknownEnumValue:
        break;
    }

    return "Unknown";
}

// Validates an optional PIN against the door lock's stored PIN credentials for a remote operation.
// If no PIN is supplied, succeeds unless the RequirePINforRemoteOperation attribute is true (defaults to not required if the attribute read fails). If a PIN is supplied, succeeds if it exact-matches any occupied PIN credential slot.
// endpointId: endpoint whose RequirePINforRemoteOperation attribute is checked.
// pin: PIN to validate, or empty to check whether a PIN is required.
// err: output; set to OperationErrorEnum::kInvalidCredential when a supplied PIN matches no stored credential.
// Returns true if no PIN was required or a match was found; false otherwise.
bool BoltLockManager::ValidatePIN(EndpointId endpointId, const Optional<ByteSpan>  &pin, OperationErrorEnum  &err) const
{
    // Assume pin is required until told otherwise
    bool requirePin = true;
    if (Status::Success != DoorLock::Attributes::RequirePINforRemoteOperation::Get(endpointId, &requirePin)) {
        requirePin = false;
    }

    // If a pin code is not given
    if (!pin.HasValue()) {
        ESP_LOGI(TAG, "Door Lock App: PIN code is not specified [endpointId=%d]", endpointId);

        // If a pin code is not required
        if (!requirePin) {
            return true;
        }
        ESP_LOGI(TAG, "Door Lock App: PIN code is not specified, but it is required [endpointId=%d]", endpointId);
        return false;
    }

    // Check the PIN code
    for (uint16_t credentialIndex = 0; credentialIndex < kMaxCredentialsPerUser; ++credentialIndex) {
        const uint16_t storageIndex = CredentialStorageIndex(credentialIndex, CredentialTypeEnum::kPin);
        const auto &credentialInStorage = mLockCredentials[storageIndex];
        if (credentialInStorage.status == DlCredentialStatus::kAvailable) {
            continue;
        }
        if (credentialInStorage.credentialData.data_equal(pin.Value())) {
            ESP_LOGI(TAG, "Lock App: specified PIN code was found in the database [endpointId=%d]", endpointId);
            return true;
        }
    }

    ESP_LOGI(TAG, "Door Lock App: specified PIN code was not found in the database [endpointId=%d]", endpointId);
    err = OperationErrorEnum::kInvalidCredential;
    return false;
}

// Initializes the door lock's runtime state: reads the initial lock state and per-endpoint user/credential capacity attributes (falling back to 5 credentials-per-user and 10 users on read failure), initializes BoltLockMgr with those limits, then loads persisted state from NVM.
// Returns CHIP_NO_ERROR on success; the error from BoltLockMgr().Init() if lock-parameter validation fails; CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND if loading persisted config fails.
CHIP_ERROR BoltLockManager::InitLockState()
{
    // Initial lock state
    DataModel::Nullable<DoorLock::DlLockState> state;
    EndpointId endpointId{ 1 };
    DoorLock::Attributes::LockState::Get(endpointId, state);

    uint8_t numberOfCredentialsPerUser = 0;
    if (!DoorLockServer::Instance().GetNumberOfCredentialsSupportedPerUser(endpointId, numberOfCredentialsPerUser)) {
        ESP_LOGE(TAG, "Unable to get number of credentials supported per user when initializing lock endpoint, defaulting to 5 [endpointId=%d]", endpointId);
        numberOfCredentialsPerUser = 5;
    }

    uint16_t numberOfUsers = 0;
    if (!DoorLockServer::Instance().GetNumberOfUserSupported(endpointId, numberOfUsers)) {
        ESP_LOGE(TAG, "Unable to get number of supported users when initializing lock endpoint, defaulting to 10 [endpointId=%d]", endpointId);
        numberOfUsers = 10;
    }

    CHIP_ERROR err = BoltLockMgr().Init(state, ParamBuilder()
                                        .SetNumberOfUsers(numberOfUsers)
                                        .SetNumberOfCredentialsPerUser(numberOfCredentialsPerUser)
                                        .GetLockParam());
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "BoltLockMgr().Init() failed");
        return err;
    }
    if (!ReadConfigValues()) {
        return CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
    }
    return err;
}
