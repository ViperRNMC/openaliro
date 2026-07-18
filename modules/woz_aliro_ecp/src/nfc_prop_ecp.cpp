// NFC Type A proprietary callback implementation for Aliro Express unlock (tap-to-unlock without
// Face ID). Emits a CRC_A–checksummed ECP frame carrying the reader identifier.
/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Apple ECP (Enhanced Contactless Polling) emitter for the Aliro tap path.
 */

#include <nfc_prop/nfc_prop.h>
#include <rfal_nfc.h>
#include <rfal_nfca.h>
#include <rfal_rf.h>

#include <aliro/errors.h>
#include <aliro/types.h>
#include <reader_storage/reader.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nfc_prop_ecp, CONFIG_DOOR_LOCK_RFAL_LOG_LEVEL);

namespace
{

constexpr std::size_t kEcpFrameLen = 18;
constexpr std::size_t kReaderIdLen = 8;

/* ECP v2 header for the Aliro (Unified Access) profile. */
constexpr std::array<uint8_t, 8> kAliroEcpHeader = {0x6A, 0x02, 0xCB, 0x02, 0x06, 0x20, 0x42, 0x20};

uint8_t sEcpFrame[kEcpFrameLen];
bool sArmed;

/**
 * @brief Computes the ISO/IEC 14443-A CRC_A checksum (initial value 0x6363) over a byte buffer.
 * @param data Pointer to the input bytes to checksum.
 * @param size Number of bytes in data to process.
 * @param result Output buffer receiving the 2-byte little-endian CRC_A result.
 */
void Crc16A(const uint8_t *data, unsigned int size, uint8_t *result)
{
	unsigned short wCrc = 0x6363;

	for (unsigned int i = 0; i < size; ++i) {
		unsigned char b = static_cast<unsigned char>(data[i] ^ (wCrc & 0x00FF));

		b = static_cast<unsigned char>((b ^ (b << 4)) & 0xFF);
		wCrc = static_cast<unsigned short>(((wCrc >> 8) ^ (b << 8) ^ (b << 3) ^ (b >> 4)) &
						   0xFFFF);
	}
	result[0] = static_cast<uint8_t>(wCrc & 0xFF);
	result[1] = static_cast<uint8_t>((wCrc >> 8) & 0xFF);
}

/**
 * @brief Initializes the RFAL NFC Type A polling stack.
 * @return Return code from rfalNfcaPollerInitialize.
 */
ReturnCode PropInit(void)
{
	return rfalNfcaPollerInitialize();
}

/**
 * @brief Transmits the armed ECP frame as a fire-and-forget beacon and reports no proprietary
 * device detected so RFAL polling can proceed.
 * @return RFAL_ERR_TIMEOUT if the frame is not armed, or RFAL_ERR_TIMEOUT after transmission to
 * signal no proprietary device found.
 */
ReturnCode PropTechDetect(void)
{
	if (!sArmed) {
		return RFAL_ERR_TIMEOUT;
	}

	uint8_t rx[32];
	uint16_t rxLen = 0;
	const uint32_t flags = static_cast<uint32_t>(RFAL_TXRX_FLAGS_CRC_TX_MANUAL) |
			       static_cast<uint32_t>(RFAL_TXRX_FLAGS_CRC_RX_KEEP);

	(void)rfalTransceiveBlockingTxRx(sEcpFrame, kEcpFrameLen, rx, sizeof rx, &rxLen, flags,
					 RFAL_NFCA_FDTMIN);

	/* Fire-and-forget beacon: report "no proprietary device" so RFAL proceeds. */
	return RFAL_ERR_TIMEOUT;
}

const rfalNfcPropCallbacks kCallbacks = {
	.rfalNfcpPollerInitialize = PropInit,
	.rfalNfcpPollerTechnologyDetection = PropTechDetect,
	.rfalNfcpPollerStartCollisionResolution = nullptr,
	.rfalNfcpPollerGetCollisionResolutionStatus = nullptr,
	.rfalNfcpStartActivation = nullptr,
	.rfalNfcpGetActivationStatus = nullptr,
};

} // namespace

/**
 * @brief Builds and arms the ECP frame with the Aliro header, provisioned reader identifier, and
 * CRC_A checksum for emission.
 */
void NfcPropInit(void)
{
	std::memcpy(sEcpFrame, kAliroEcpHeader.data(), kAliroEcpHeader.size());

	Aliro::Identifier identifier{};
	if (DoorLock::ReaderStorage::IsIdentifierSet() &&
	    DoorLock::ReaderStorage::GetIdentifier(identifier) == ALIRO_NO_ERROR) {
		std::memcpy(sEcpFrame + kAliroEcpHeader.size(), identifier.data(), kReaderIdLen);
	} else {
		LOG_WRN("ECP: reader identifier not provisioned; emitting zero Reader Identifier");
		std::memset(sEcpFrame + kAliroEcpHeader.size(), 0, kReaderIdLen);
	}

	Crc16A(sEcpFrame, 16, sEcpFrame + 16);
	sArmed = true;

	LOG_HEXDUMP_INF(sEcpFrame, kEcpFrameLen, "ECP Aliro frame armed:");
}

/**
 * @brief Returns the RFAL proprietary NFC callback table for Aliro ECP emission.
 * @return Pointer to the static rfalNfcPropCallbacks table.
 */
const rfalNfcPropCallbacks *NfcPropGetCallbacks(void)
{
	return &kCallbacks;
}
