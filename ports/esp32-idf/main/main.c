/*
 * Woz UWB ranging engine on ESP32-S3 (ESP-IDF) — minimal bring-up app.
 *
 * Binds a canned URSK and starts the CCC DS-TWR responder on the DW3000, then
 * polls for a range. With no iPhone/initiator present this proves the SPI +
 * DW3000 + CCC init path comes up on ESP32-S3; a live range needs a peer that
 * drives the DS-TWR exchange (an Aliro Wallet, or a second board as initiator).
 *
 * The demo responder lifecycle + interactive console live in app_shell.c.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "woz_uwb_facade.h"
#include "aliro_reader.h"
#include "app_shell.h"

static const char *TAG = "woz_esp32s3";

// Application entry point: brings up the DW3000 responder, the Aliro BLE reader,
// and the interactive shell, then polls for ranging results.
// Silences the CCC shim's per-frame STS trace (WARN level only) because logging
// on the delayed-TX reply path can blow the reply window; other subsystems keep
// their normal log level. app_responder_start() performs the full DW3000
// bring-up chain (woz_uwb_start_aliro -> ccc_prepoll_listen ->
// uwb_min_radio_init). aliro_reader_start() brings up the BLE transport and
// session/transaction layer independently of the demo responder; the
// URSK-driven UWB start happens inside the reader once the Phase-3 handshake is
// implemented. Never returns: loops forever logging the last UWB range every
// 500 ms when available.
void app_main(void)
{
	/* Mute the CCC shim's first-8-frames STS trace: it fires on the per-frame
	 * TX-arm (delayed-TX reply) path, and a log line there can blow the reply
	 * window. WARN keeps its errors. Shared engine source is left untouched;
	 * this is the runtime log-level lever instead. */
	esp_log_level_set("ccc_shim", ESP_LOG_WARN);

	/* app_responder_start() drives the full DW3000 bring-up internally
	 * (woz_uwb_start_aliro -> ccc_prepoll_listen -> uwb_min_radio_init). */
	int rc = app_responder_start();
	ESP_LOGI(TAG, "app_responder_start() = %d %s", rc,
		 rc == 0 ? "(DW3000 up, responder listening)"
			 : "(FAILED -- check wiring/SPI)");

	/* Phase 2: bring up the Aliro reader (BLE transport + session/transaction
	 * layer). Additive; independent of the demo UWB responder above. The real
	 * URSK-driven UWB start happens inside the reader once the Phase-3 handshake
	 * is implemented. */
	int brc = aliro_reader_start();
	ESP_LOGI(TAG, "aliro_reader_start() = %d %s", brc,
		 brc == 0 ? "(Aliro reader up)" : "(reader bring-up FAILED)");

	/* Interactive console on the console UART (shares the log stream). */
	app_shell_start();

	while (1) {
		int32_t cm;

		if (woz_uwb_last_range_cm(&cm)) {
			ESP_LOGI(TAG, "range: %d cm", (int)cm);
		}
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}
