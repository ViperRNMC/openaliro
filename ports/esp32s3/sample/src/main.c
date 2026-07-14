/*
 * Woz UWB ranging engine on ESP32-S3 — minimal bring-up sample.
 *
 * Binds a canned URSK and starts the CCC DS-TWR responder on the DW3000, then
 * polls for a range. With no iPhone/initiator present this proves the SPI +
 * DW3000 + CCC init path comes up on ESP32-S3; a live range needs a peer that
 * drives the DS-TWR exchange (an Aliro Wallet, or a second board as initiator).
 *
 * Reference scaffold (see ports/esp32s3/README.md) — not yet built on silicon.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "woz_uwb_facade.h"

LOG_MODULE_REGISTER(woz_esp32s3, LOG_LEVEL_INF);

/* Dummy 32-byte URSK for a peerless bring-up smoke test (mirrors uwb_selftest.c). */
static const uint8_t demo_ursk[32] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
	0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
	0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
};

int main(void)
{
	const struct woz_uwb_aliro_cfg cfg = {
		.session_id = 0x02b02fd4u,
		.channel = 9u,
		.sync_code_index = 9u,
		.slot_duration_rstu = 2400u,
		.block_duration_ms = 192u,
		.slot_per_round = 12u,
		.sts_index0 = 0x1196e79du,
		.uwb_time_us = 0u,
		.ursk = demo_ursk,
	};
	int rc = woz_uwb_start_aliro(&cfg);

	LOG_INF("woz_uwb_start_aliro() = %d %s", rc,
		rc == 0 ? "(DW3000 up, responder listening)"
			: "(FAILED -- check wiring/SPI)");

	while (1) {
		int32_t cm;

		if (woz_uwb_last_range_cm(&cm)) {
			LOG_INF("range: %d cm", cm);
		}
		k_sleep(K_MSEC(500));
	}
	return 0;
}
