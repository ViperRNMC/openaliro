// Aliro BLE advertisement Dynamic Tag derivation (Aliro 1.0 section 11.3.1), shared by the
// BLE transport (live advertising) and the host KAT suite (spec section 20 worked examples).
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * plaintextData = Pad_Bytes(6 x 00) || AdvA || Dynamic Tag Expiry Timestamp,
 * all MSB-first; the tag is the 7 most significant bytes of
 * AES-128-ECB(GRK, plaintextData). KAT'd in ports/esp32/test against the three
 * section 20 vectors, so a PASS there pins this exact byte layout.
 */
#include "aliro_advtag.h"

#include <string.h>

#include "aliro_prim.h"

int aliro_advtag_derive(const uint8_t grk[16], const uint8_t adva_msb[6], uint32_t expiry_unix,
			uint8_t tag[ALIRO_ADVTAG_LEN])
{
	uint8_t block[16] = {0}; /* [0..5] = Pad_Bytes */
	uint8_t enc[16];

	memcpy(&block[6], adva_msb, 6);
	block[12] = (uint8_t)(expiry_unix >> 24);
	block[13] = (uint8_t)(expiry_unix >> 16);
	block[14] = (uint8_t)(expiry_unix >> 8);
	block[15] = (uint8_t)expiry_unix;

	int rc = aliro_aes128_ecb_encrypt(grk, block, enc);

	if (rc != 0) {
		return rc;
	}
	memcpy(tag, enc, ALIRO_ADVTAG_LEN);
	return 0;
}
