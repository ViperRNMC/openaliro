/*
 * Woz UWB ranging engine on ESP32-S3 (ESP-IDF) — minimal bring-up app.
 *
 * Binds a canned URSK and starts the CCC DS-TWR responder on the DW3000, then
 * polls for a range. With no iPhone/initiator present this proves the SPI +
 * DW3000 + CCC init path comes up on ESP32-S3; a live range needs a peer that
 * drives the DS-TWR exchange (an Aliro Wallet, or a second board as initiator).
 *
 * Ported from ports/esp32s3/sample/src/main.c (the Zephyr scaffold).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "woz_uwb_facade.h"
#include "aliro_reader.h"

static const char *TAG = "woz_esp32s3";

/* Dummy 32-byte URSK for a peerless bring-up smoke test (mirrors uwb_selftest.c). */
static const uint8_t demo_ursk[32] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
	0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
	0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
};

void app_main(void)
{
	/* woz_uwb_start_aliro() drives the full DW3000 bring-up internally
	 * (ccc_prepoll_listen -> uwb_min_radio_init). */
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
	ESP_LOGI(TAG, "woz_uwb_start_aliro() = %d %s", rc,
		 rc == 0 ? "(DW3000 up, responder listening)"
			 : "(FAILED -- check wiring/SPI)");

	/* Phase 2: bring up the Aliro reader (BLE transport + session/transaction
	 * layer). Additive; independent of the demo UWB responder above. The real
	 * URSK-driven UWB start happens inside the reader once the Phase-3 handshake
	 * is implemented. */
	int brc = aliro_reader_start();
	ESP_LOGI(TAG, "aliro_reader_start() = %d %s", brc,
		 brc == 0 ? "(Aliro reader up)" : "(reader bring-up FAILED)");

	while (1) {
		int32_t cm;

		if (woz_uwb_last_range_cm(&cm)) {
			ESP_LOGI(TAG, "range: %d cm", (int)cm);
		}
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}
