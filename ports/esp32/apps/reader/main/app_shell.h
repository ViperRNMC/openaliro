/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * app_shell — interactive console for the ESP32-S3 bring-up app.
 *
 * Runs an esp_console REPL on the default console UART (shares the UART0 log
 * stream, like the nRF Zephyr shell), so `make monitor`/`make term` need no
 * change. Commands drive the demo DS-TWR responder lifecycle. The responder
 * start/stop is serialized by
 * a mutex and tracked by a flag, so the boot-time start and shell commands can
 * never race, and a start-while-running is rejected instead of double-started.
 *
 * The REPL task runs at low priority pinned off the DW3000 responder core, so it
 * cannot preempt the timing-critical radio work.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start the demo CCC DS-TWR responder (canned URSK/cfg). Serialized by mutex.
 *  Returns 0 on success, 1 if already running, negative on facade failure. */
int app_responder_start(void);

/** Stop the demo responder and unbind the CCC shim. No-op if already stopped. */
void app_responder_stop(void);

/** True while the demo responder is up. */
bool app_responder_up(void);

/** Register commands and start the UART console REPL (own task). */
void app_shell_start(void);

#ifdef __cplusplus
}
#endif
