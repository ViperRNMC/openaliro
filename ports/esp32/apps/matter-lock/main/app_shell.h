/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * app_shell — interactive console for the ESP32-S3 Matter door-lock app.
 *
 * Replaces the CHIP shell (chip::Shell::Engine::RunMainLoop), which is a raw
 * streamer line-reader: no history, no completion, no arrow keys, and a log line
 * arriving mid-input clobbers what you typed. This is an esp_console REPL over
 * linenoise instead, matching the bench reader app's shell, and it is the sole
 * reader of the console UART — running both would fight over the same fd.
 *
 * Commands that touch Matter state either schedule onto the Matter task or take
 * the CHIP stack lock, so the REPL task never races the stack.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Register commands and start the console REPL (own task, pinned to core 0). */
void app_shell_start(void);

#ifdef __cplusplus
}
#endif
