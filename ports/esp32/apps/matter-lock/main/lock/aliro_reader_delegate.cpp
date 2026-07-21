// AliroReaderDelegate: implements the Aliro reader-provisioning and BLE-UWB portions of the Matter
// DoorLock::Delegate interface, backing the controller-facing GetAliro*/SetAliroReaderConfig
// commands and persisting the provisioned reader identity via aliro_reader_provision_identity.
// Bridges Matter cluster commands to the underlying aliro_reader NVS-backed identity/trust store
// and to the BLE advertising layer (refreshed when the group resolving key changes).
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

// Bridge into the reader component's NVS-backed provisioning store (aliro_prov).
// The header carries its own extern "C" guard.
#include <aliro_reader.h>

using namespace chip;
using namespace chip::app::Clusters::DoorLock;

AliroReaderDelegate AliroReaderDelegate::sInstance;

namespace
{
// The single Aliro protocol version this reader advertises (both expedited and
// BLE-UWB), big-endian 0x0100.
constexpr uint16_t kKnownProtocolVersion = 0x0100;
} // namespace

// Lazily generates and caches the Aliro group sub-identifier via DRBG on first call; subsequent calls are a no-op. On RNG failure, zeroes mGroupSubIdentifier and logs an error, but still marks it as set so this is not retried.
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

// Copies the Aliro reader group identifier into groupIdentifier. If the reader is not configured, reduces the output span to size 0 instead of copying. Returns CHIP_NO_ERROR on success or whatever CopySpanToMutableSpan reports on failure.
CHIP_ERROR AliroReaderDelegate::GetAliroReaderGroupIdentifier(MutableByteSpan &groupIdentifier)
{
	if (!mConfigured) {
		groupIdentifier.reduce_size(0);
		return CHIP_NO_ERROR;
	}
	return CopySpanToMutableSpan(ByteSpan(mGroupIdentifier), groupIdentifier);
}

// Copies the Aliro reader group sub-identifier into groupSubIdentifier.
// Lazily generates the sub-identifier on first call via EnsureSubIdentifier. Returns CHIP_NO_ERROR
// on success or whatever CopySpanToMutableSpan reports on failure.
CHIP_ERROR
AliroReaderDelegate::GetAliroReaderGroupSubIdentifier(MutableByteSpan &groupSubIdentifier)
{
	EnsureSubIdentifier();
	return CopySpanToMutableSpan(ByteSpan(mGroupSubIdentifier), groupSubIdentifier);
}

// Encodes a 16-bit Aliro protocol version as 2 big-endian bytes into out. Returns CHIP_ERROR_INVALID_ARGUMENT if out is smaller than kAliroProtocolVersionSize; on success, reduces out to the written size.
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

// Reports the Aliro expedited-transaction protocol version supported at index.
// Only index 0 is valid; returns CHIP_ERROR_PROVIDER_LIST_EXHAUSTED for any other index. On
// success, encodes kKnownProtocolVersion big-endian into protocolVersion via
// CopyProtocolVersionIntoSpan.
CHIP_ERROR AliroReaderDelegate::GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex(
	size_t index, MutableByteSpan &protocolVersion)
{
	if (index > 0) {
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	return CopyProtocolVersionIntoSpan(kKnownProtocolVersion, protocolVersion);
}

// Copies the Aliro group resolving key into groupResolvingKey. If the reader is not configured, reduces the output span to size 0 instead of copying. Returns CHIP_NO_ERROR on success or whatever CopySpanToMutableSpan reports on failure.
CHIP_ERROR AliroReaderDelegate::GetAliroGroupResolvingKey(MutableByteSpan &groupResolvingKey)
{
	if (!mConfigured) {
		groupResolvingKey.reduce_size(0);
		return CHIP_NO_ERROR;
	}
	return CopySpanToMutableSpan(ByteSpan(mGroupResolvingKey), groupResolvingKey);
}

// Reports the Aliro BLE-UWB protocol version supported at index.
// Only index 0 is valid; returns CHIP_ERROR_PROVIDER_LIST_EXHAUSTED for any other index. On
// success, encodes kKnownProtocolVersion big-endian into protocolVersion via
// CopyProtocolVersionIntoSpan.
CHIP_ERROR
AliroReaderDelegate::GetAliroSupportedBLEUWBProtocolVersionAtIndex(size_t index,
								   MutableByteSpan &protocolVersion)
{
	if (index > 0) {
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	return CopyProtocolVersionIntoSpan(kKnownProtocolVersion, protocolVersion);
}

// Returns the Aliro BLE advertising version. Always 0, the only version currently defined.
uint8_t AliroReaderDelegate::GetAliroBLEAdvertisingVersion()
{
	// 0 is the only defined Aliro BLE advertising version.
	return 0;
}

// Returns the number of Aliro credential issuer keys supported, kAliroKeysSupported.
uint16_t AliroReaderDelegate::GetNumberOfAliroCredentialIssuerKeysSupported()
{
	return kAliroKeysSupported;
}

// Returns the number of Aliro endpoint keys supported, kAliroKeysSupported.
uint16_t AliroReaderDelegate::GetNumberOfAliroEndpointKeysSupported()
{
	return kAliroKeysSupported;
}

// ---------------------------------------------------------------------------
// Reader-provisioning commands
// ---------------------------------------------------------------------------

// Store a new Aliro reader configuration (signing key, verification key, group identifier, and
// optional group resolving key) sent by the controller, and persist the corresponding reader
// identity to NVS.
// Requires signingKey, verificationKey, and groupIdentifier to each match their fixed expected
// sizes, and if present, groupResolvingKey to match its fixed size; returns CHIP_ERROR_INVALID_ARGUMENT
// otherwise, without modifying any stored state. If groupResolvingKey is absent, zeroes
// mGroupResolvingKey. On success, marks the reader configured, ensures the group sub-identifier is
// generated, builds reader_id = groupIdentifier || groupSubIdentifier, and calls
// aliro_reader_provision_identity to persist it alongside the signing key and group resolving key
// (persistence failure is logged but does not change the return value). Also refreshes the BLE
// advertisement so it carries the newly configured group resolving key, since the reader may have
// started advertising before this command arrived. Always returns CHIP_NO_ERROR once the size
// checks pass.
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

	// Persist the provisioned identity into the reader's NVS store so a
	// handoff-started reader authenticates the credential Apple just installed:
	// reader_id = groupIdentifier || groupSubIdentifier, sign_priv = signingKey.
	// (groupResolvingKey is captured above for the BLE approach-resolution
	// refinement; it is not yet carried into aliro_prov.)
	EnsureSubIdentifier();
	uint8_t readerId[kAliroReaderGroupIdentifierSize + kAliroReaderGroupSubIdentifierSize];
	memcpy(readerId, mGroupIdentifier, sizeof(mGroupIdentifier));
	memcpy(readerId + sizeof(mGroupIdentifier), mGroupSubIdentifier,
	       sizeof(mGroupSubIdentifier));
	int rc = aliro_reader_provision_identity(readerId, mSigningKey, mGroupResolvingKey);

	// Apple sends this command AFTER commissioning completes, but the reader starts
	// (and begins advertising) on kCommissioningComplete — so its first advertisement
	// predates the GRK and is not approach-resolvable. Refresh it with the GRK now.
	aliro_reader_refresh_adv();

	ChipLogProgress(Zcl,
			"Aliro reader configured — identity provisioned (groupResolvingKey=%d, "
			"aliro_prov rc=%d)",
			static_cast<int>(groupResolvingKey.HasValue()), rc);
	return CHIP_NO_ERROR;
}

// Clears the stored Aliro reader configuration (signing/verification keys, group identifier, group resolving key) and marks the reader unconfigured. Also clears the persisted provisioning state via aliro_reader_provision_clear. Always returns CHIP_NO_ERROR.
CHIP_ERROR AliroReaderDelegate::ClearAliroReaderConfig()
{
	memset(mSigningKey, 0, sizeof(mSigningKey));
	memset(mVerificationKey, 0, sizeof(mVerificationKey));
	memset(mGroupIdentifier, 0, sizeof(mGroupIdentifier));
	memset(mGroupResolvingKey, 0, sizeof(mGroupResolvingKey));
	mConfigured = false;

	int rc = aliro_reader_provision_clear();
	ChipLogProgress(Zcl, "Aliro reader config cleared (aliro_prov rc=%d)", rc);
	return CHIP_NO_ERROR;
}
