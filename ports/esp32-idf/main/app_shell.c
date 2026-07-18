/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * app_shell — see app_shell.h. Interactive console + demo responder lifecycle.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "esp_log.h"
#include "esp_err.h"

#include "woz_uwb_facade.h"
#include "aliro_reader.h"
#include "app_shell.h"

static const char *TAG = "shell";

/* Dummy 32-byte URSK for a peerless bring-up smoke test (mirrors uwb_selftest.c).
 * Moved here from main.c so both the boot-time start and the `aliro-start`
 * command drive the exact same canned credential. */
static const uint8_t demo_ursk[32] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
	0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
	0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
};

static const struct woz_uwb_aliro_cfg demo_cfg = {
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

/* Single-owner guard for the DW3000 bring-up path: serializes boot + shell
 * start/stop so they can never double-start or race. s_up tracks state. */
static SemaphoreHandle_t s_lock;
static bool s_up;

static void lock_init(void)
{
	if (s_lock == NULL) {
		s_lock = xSemaphoreCreateMutex();
	}
}

int app_responder_start(void)
{
	lock_init();
	xSemaphoreTake(s_lock, portMAX_DELAY);
	int rc = 0;
	if (s_up) {
		rc = 1; /* already running */
	} else {
		rc = woz_uwb_start_aliro(&demo_cfg);
		if (rc == 0) {
			s_up = true;
		}
	}
	xSemaphoreGive(s_lock);
	return rc;
}

void app_responder_stop(void)
{
	lock_init();
	xSemaphoreTake(s_lock, portMAX_DELAY);
	if (s_up) {
		woz_uwb_stop();
		s_up = false;
	}
	xSemaphoreGive(s_lock);
}

bool app_responder_up(void)
{
	return s_up;
}

/* ---- console commands -------------------------------------------------- *
 * Handlers run on the REPL task (low prio, off the radio core) and only call
 * thread-safe facade accessors or the mutex-guarded lifecycle helpers, so they
 * never touch the DW3000 bus concurrently with the responder. */

static int cmd_status(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int32_t cm;
	printf("responder : %s\n", app_responder_up() ? "up" : "down");
	if (woz_uwb_last_range_cm(&cm)) {
		printf("last range: %d cm\n", (int)cm);
	} else {
		printf("last range: none\n");
	}
	if (woz_uwb_trusted_range_cm(&cm)) {
		printf("trusted   : %d cm\n", (int)cm);
	} else {
		printf("trusted   : none\n");
	}
	return 0;
}

static int cmd_range(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int32_t cm;
	if (woz_uwb_last_range_cm(&cm)) {
		printf("range: %d cm\n", (int)cm);
	} else {
		printf("no range yet\n");
	}
	return 0;
}

static int cmd_aliro_start(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int rc = app_responder_start();
	if (rc == 1) {
		printf("busy: responder already running\n");
	} else {
		printf("aliro-start: %s (rc=%d)\n", rc == 0 ? "ok" : "FAILED", rc);
	}
	return 0;
}

static int cmd_aliro_stop(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	app_responder_stop();
	printf("aliro-stop: ok\n");
	return 0;
}

static int cmd_aliro_prov(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	aliro_reader_prov_print();
	return 0;
}

static int cmd_clear(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	linenoiseClearScreen();
	return 0;
}

static int cmd_aliro_trust(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int rc = aliro_reader_trust_last();

	if (rc == 0) {
		printf("aliro-trust: added last-presented credential + saved to NVS\n");
	} else if (rc == 1) {
		printf("aliro-trust: nothing to add (no credential presented, or "
		       "already trusted)\n");
	} else {
		printf("aliro-trust: FAILED (trust store full or NVS error)\n");
	}
	return 0;
}

void app_shell_start(void)
{
	esp_console_repl_t *repl = NULL;
	esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
	/* Defaults already give prio 2 + history_save_path = NULL (no flash writes,
	 * which would stall both cores' cache). Pin off the responder core (core 1). */
	repl_cfg.prompt = "esp32>";
	repl_cfg.task_core_id = 0;

	/* UART repl on the default console UART: the prompt shares the UART0 log
	 * stream, so `make monitor`/`make term` need no change. This mirrors the
	 * nRF Zephyr shell (shell + logs interleaved on one UART). */
	esp_console_dev_uart_config_t dev_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

	ESP_ERROR_CHECK(esp_console_new_repl_uart(&dev_cfg, &repl_cfg, &repl));

	/* esp_console defaults to multiline mode + a hints callback; either one
	 * forces linenoise to redraw prompt+line on every keystroke, which visibly
	 * flickers the cursor over the UART. With both off, typing echoes only the
	 * typed character (tab completion still works). */
	linenoiseSetMultiLine(0);
	linenoiseSetHintsCallback(NULL);

	const esp_console_cmd_t cmds[] = {
		{.command = "status",
		 .help = "responder state + last/trusted range",
		 .func = cmd_status},
		{.command = "range", .help = "print the latest distance", .func = cmd_range},
		{.command = "aliro-start",
		 .help = "start the demo DS-TWR responder",
		 .func = cmd_aliro_start},
		{.command = "aliro-stop",
		 .help = "stop the demo responder",
		 .func = cmd_aliro_stop},
		{.command = "aliro-prov",
		 .help = "show reader identity + credential trust store",
		 .func = cmd_aliro_prov},
		{.command = "aliro-trust",
		 .help = "trust the last-presented credential (persist to NVS)",
		 .func = cmd_aliro_trust},
		{.command = "clear", .help = "clear the screen (also: ctrl-L)", .func = cmd_clear},
	};
	for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
	}
	ESP_ERROR_CHECK(esp_console_register_help_command());

	ESP_ERROR_CHECK(esp_console_start_repl(repl));
	ESP_LOGI(TAG, "console up on the UART (type 'help')");
}
