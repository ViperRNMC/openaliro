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

#include "aliro_reader_delegate.h"

#include <crypto/CHIPCryptoPAL.h>
#include <lib/core/CHIPEncoding.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>

#include <string.h>

using namespace chip;
using namespace chip::app::Clusters::DoorLock;

AliroReaderDelegate AliroReaderDelegate::sInstance;

namespace
{
// The single Aliro protocol version this reader advertises (both expedited and
// BLE-UWB), big-endian 0x0100.
constexpr uint16_t kKnownProtocolVersion = 0x0100;
} // namespace

void AliroReaderDelegate::EnsureSubIdentifier()
{
	if (mHasSubId) {
		return;
	}
	CHIP_ERROR err = Crypto::DRBG_get_bytes(mGroupSubIdentifier, sizeof(mGroupSubIdentifier));
	if (err != CHIP_NO_ERROR) {
		ChipLogError(Zcl, "Aliro: sub-identifier RNG failed: %" CHIP_ERROR_FORMAT,
			     err.Format());
		memset(mGroupSubIdentifier, 0, sizeof(mGroupSubIdentifier));
	}
	mHasSubId = true;
}

void AliroReaderDelegate::Init()
{
	EnsureSubIdentifier();
	ChipLogProgress(Zcl, "Aliro reader delegate ready (configured=%d)",
			static_cast<int>(mConfigured));
}

// ---------------------------------------------------------------------------
// Reader-provisioning attribute getters
// ---------------------------------------------------------------------------

CHIP_ERROR AliroReaderDelegate::GetAliroReaderVerificationKey(MutableByteSpan &verificationKey)
{
	if (!mConfigured) {
		verificationKey.reduce_size(0);
		return CHIP_NO_ERROR;
	}
	return CopySpanToMutableSpan(ByteSpan(mVerificationKey), verificationKey);
}

CHIP_ERROR AliroReaderDelegate::GetAliroReaderGroupIdentifier(MutableByteSpan &groupIdentifier)
{
	if (!mConfigured) {
		groupIdentifier.reduce_size(0);
		return CHIP_NO_ERROR;
	}
	return CopySpanToMutableSpan(ByteSpan(mGroupIdentifier), groupIdentifier);
}

CHIP_ERROR
AliroReaderDelegate::GetAliroReaderGroupSubIdentifier(MutableByteSpan &groupSubIdentifier)
{
	EnsureSubIdentifier();
	return CopySpanToMutableSpan(ByteSpan(mGroupSubIdentifier), groupSubIdentifier);
}

CHIP_ERROR AliroReaderDelegate::CopyProtocolVersionIntoSpan(uint16_t value, MutableByteSpan &out)
{
	static_assert(sizeof(value) == kAliroProtocolVersionSize, "protocol version is 2 bytes");

	if (out.size() < kAliroProtocolVersionSize) {
		return CHIP_ERROR_INVALID_ARGUMENT;
	}
	// Aliro protocol versions are encoded big-endian.
	Encoding::BigEndian::Put16(out.data(), value);
	out.reduce_size(kAliroProtocolVersionSize);
	return CHIP_NO_ERROR;
}

CHIP_ERROR AliroReaderDelegate::GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex(
	size_t index, MutableByteSpan &protocolVersion)
{
	if (index > 0) {
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	return CopyProtocolVersionIntoSpan(kKnownProtocolVersion, protocolVersion);
}

CHIP_ERROR AliroReaderDelegate::GetAliroGroupResolvingKey(MutableByteSpan &groupResolvingKey)
{
	if (!mConfigured) {
		groupResolvingKey.reduce_size(0);
		return CHIP_NO_ERROR;
	}
	return CopySpanToMutableSpan(ByteSpan(mGroupResolvingKey), groupResolvingKey);
}

CHIP_ERROR
AliroReaderDelegate::GetAliroSupportedBLEUWBProtocolVersionAtIndex(size_t index,
								   MutableByteSpan &protocolVersion)
{
	if (index > 0) {
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	return CopyProtocolVersionIntoSpan(kKnownProtocolVersion, protocolVersion);
}

uint8_t AliroReaderDelegate::GetAliroBLEAdvertisingVersion()
{
	// 0 is the only defined Aliro BLE advertising version.
	return 0;
}

uint16_t AliroReaderDelegate::GetNumberOfAliroCredentialIssuerKeysSupported()
{
	return kAliroKeysSupported;
}

uint16_t AliroReaderDelegate::GetNumberOfAliroEndpointKeysSupported()
{
	return kAliroKeysSupported;
}

// ---------------------------------------------------------------------------
// Reader-provisioning commands
// ---------------------------------------------------------------------------

CHIP_ERROR AliroReaderDelegate::SetAliroReaderConfig(const ByteSpan &signingKey,
						     const ByteSpan &verificationKey,
						     const ByteSpan &groupIdentifier,
						     const Optional<ByteSpan> &groupResolvingKey)
{
	VerifyOrReturnError(signingKey.size() == sizeof(mSigningKey), CHIP_ERROR_INVALID_ARGUMENT);
	VerifyOrReturnError(verificationKey.size() == sizeof(mVerificationKey),
			    CHIP_ERROR_INVALID_ARGUMENT);
	VerifyOrReturnError(groupIdentifier.size() == sizeof(mGroupIdentifier),
			    CHIP_ERROR_INVALID_ARGUMENT);

	memcpy(mSigningKey, signingKey.data(), sizeof(mSigningKey));
	memcpy(mVerificationKey, verificationKey.data(), sizeof(mVerificationKey));
	memcpy(mGroupIdentifier, groupIdentifier.data(), sizeof(mGroupIdentifier));

	if (groupResolvingKey.HasValue()) {
		VerifyOrReturnError(groupResolvingKey.Value().size() == sizeof(mGroupResolvingKey),
				    CHIP_ERROR_INVALID_ARGUMENT);
		memcpy(mGroupResolvingKey, groupResolvingKey.Value().data(),
		       sizeof(mGroupResolvingKey));
	} else {
		memset(mGroupResolvingKey, 0, sizeof(mGroupResolvingKey));
	}

	mConfigured = true;
	ChipLogProgress(Zcl,
			"Aliro reader configured — identity provisioned (groupResolvingKey=%d)",
			static_cast<int>(groupResolvingKey.HasValue()));
	return CHIP_NO_ERROR;
}

CHIP_ERROR AliroReaderDelegate::ClearAliroReaderConfig()
{
	memset(mSigningKey, 0, sizeof(mSigningKey));
	memset(mVerificationKey, 0, sizeof(mVerificationKey));
	memset(mGroupIdentifier, 0, sizeof(mGroupIdentifier));
	memset(mGroupResolvingKey, 0, sizeof(mGroupResolvingKey));
	mConfigured = false;
	ChipLogProgress(Zcl, "Aliro reader config cleared");
	return CHIP_NO_ERROR;
}
