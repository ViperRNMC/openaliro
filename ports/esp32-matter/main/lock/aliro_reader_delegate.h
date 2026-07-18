/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <app/clusters/door-lock-server/door-lock-delegate.h>
#include <app/clusters/door-lock-server/door-lock-server.h>

/*
 * AliroReaderDelegate — the Aliro (Apple Home Key) reader-provisioning half of
 * the Door Lock cluster Delegate, for a BLE + UWB ("Express") reader.
 *
 * Apple Home writes the reader identity with SetAliroReaderConfig (signing key,
 * verification key, group identifier, and — because kAliroBLEUWB is advertised —
 * the group resolving key) and reads the reader attributes back to confirm; this
 * delegate holds that config and advertises the expedited + BLE-UWB protocol
 * versions the reader speaks.
 *
 * Aliro credential *keys* (issuer / endpoint, credential types 6/7/8) do NOT
 * flow through this Delegate. They arrive on the generic
 * emberAfPluginDoorLockSet/GetCredential path and are stored by BoltLockManager
 * (its storage is credential-type indexed and already sized to hold them), so
 * this class implements only the reader-config attributes plus the
 * "number of keys supported" getters the server consults to validate those
 * credential writes.
 *
 * State is in-memory only: Apple provisions the reader within one setup session,
 * so a read-back within that session sees the values written. Persisting the
 * provisioned identity across reboot (so a Wallet key stays valid, and the UWB
 * reader can start after a power cycle) is wired into aliro_prov when the reader
 * components are merged in.
 */
class AliroReaderDelegate: public chip::app::Clusters::DoorLock::Delegate
{
      public:
	static AliroReaderDelegate &Instance()
	{
		return sInstance;
	}

	// Generate/settle the reader group sub-identifier. Call once from app_main
	// (after nvs_flash_init, before esp_matter::start).
	void Init();

	AliroReaderDelegate(const AliroReaderDelegate &) = delete;
	AliroReaderDelegate &operator=(const AliroReaderDelegate &) = delete;

	// DoorLock::Delegate — Aliro reader-provisioning interface
	CHIP_ERROR GetAliroReaderVerificationKey(chip::MutableByteSpan &verificationKey) override;
	CHIP_ERROR GetAliroReaderGroupIdentifier(chip::MutableByteSpan &groupIdentifier) override;
	CHIP_ERROR
	GetAliroReaderGroupSubIdentifier(chip::MutableByteSpan &groupSubIdentifier) override;
	CHIP_ERROR GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex(
		size_t index, chip::MutableByteSpan &protocolVersion) override;
	CHIP_ERROR GetAliroGroupResolvingKey(chip::MutableByteSpan &groupResolvingKey) override;
	CHIP_ERROR GetAliroSupportedBLEUWBProtocolVersionAtIndex(
		size_t index, chip::MutableByteSpan &protocolVersion) override;
	uint8_t GetAliroBLEAdvertisingVersion() override;
	uint16_t GetNumberOfAliroCredentialIssuerKeysSupported() override;
	uint16_t GetNumberOfAliroEndpointKeysSupported() override;
	CHIP_ERROR
	SetAliroReaderConfig(const chip::ByteSpan &signingKey,
			     const chip::ByteSpan &verificationKey,
			     const chip::ByteSpan &groupIdentifier,
			     const chip::Optional<chip::ByteSpan> &groupResolvingKey) override;
	CHIP_ERROR ClearAliroReaderConfig() override;

      private:
	AliroReaderDelegate() = default;

	CHIP_ERROR CopyProtocolVersionIntoSpan(uint16_t value, chip::MutableByteSpan &out);
	void EnsureSubIdentifier();

	// Number of Aliro credential-issuer / endpoint keys advertised as supported.
	// Must stay <= BoltLockManager's kMaxCredentialsPerUser, since that is where
	// these credentials (types 6/7/8) are actually stored.
	static constexpr uint16_t kAliroKeysSupported = 10;

	uint8_t mVerificationKey[chip::app::Clusters::DoorLock::kAliroReaderVerificationKeySize] = {
		0};
	uint8_t mGroupIdentifier[chip::app::Clusters::DoorLock::kAliroReaderGroupIdentifierSize] = {
		0};
	uint8_t mGroupSubIdentifier
		[chip::app::Clusters::DoorLock::kAliroReaderGroupSubIdentifierSize] = {0};
	uint8_t mGroupResolvingKey[chip::app::Clusters::DoorLock::kAliroGroupResolvingKeySize] = {
		0};
	uint8_t mSigningKey[chip::app::Clusters::DoorLock::kAliroSigningKeySize] = {0};

	bool mConfigured = false;
	bool mHasSubId = false;

	static AliroReaderDelegate sInstance;
};
