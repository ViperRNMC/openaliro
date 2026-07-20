<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/shell/aliro_shell.c`

@file aliro_shell.c — `aliro` UART shell command: colored console over the UWB engine.

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](../modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_min.h`](../modules.woz_uwb.src.driver/uwb_min.h.md), [`modules/woz_uwb/src/driver/uwb_rxdiag.h`](../modules.woz_uwb.src.driver/uwb_rxdiag.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](../modules.woz_uwb.src.fira/fira_session.h.md), [`ports/esp32-idf/components/woz_uwb/compat/zephyr/kernel.h`](../ports.esp32-idf.components.woz_uwb.compat.zephyr/kernel.h.md)

## API

### `static void hdr(const struct shell *sh, const char *title)`
`modules/woz_uwb/src/shell/aliro_shell.c:30`

@brief Section header: green "aliro · <title>" over a dim rule.

**called by** `cmd_chip`, `cmd_range`, `cmd_rx`, `cmd_selftest`, `cmd_status`, `cmd_version`

### `static int cmd_chip(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:43`

Read and display the DW3110 DEV_ID register via SPI; print chip identification and verify it
matches UWB_DW3110_DEV_ID.

**calls** `hdr`

### `static int cmd_rx(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:64`

Display RX/TX tally: good frames, errors, timeouts, TX completions, and last error/success status
words.

**calls** `hdr`

### `static int cmd_range(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:83`

Display the last valid DS-TWR distance measurement: distance (cm), peer address, NLOS flag, block
number, and age since measurement.

**calls** `hdr`

### `static int cmd_selftest(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:113`

Run radio TX/RX self-test and display results: TX done, RX armed, RX event flags and raw TX/RX
status words.

**calls** `hdr`

### `struct uwb_selftest_result r =`
`modules/woz_uwb/src/shell/aliro_shell.c:119`

Uninitialized self-test result structure, filled by uwb_min_selftest.

### `static int cmd_log(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:136`

Enable or disable ranging heartbeat log output; with no argument, display current state.

### `static int cmd_frames(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:154`

Enable or disable per-block distance stream output; with no argument, display current state.

### `static int cmd_version(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:173`

Display build commit SHA (WOZ_GIT_SHA).

**calls** `hdr`

### `static int cmd_status(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:185`

Print all system status at a glance: chip ID, CCC bind state, URSK provisioning, last range
(distance and age), RX tally, and stream state (log and frames).

**calls** `hdr`

### `static int cmd_aliro(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:242`

Print aliro shell command help: lists all subcommands (status, rx, range, chip, selftest, log,
frames, version) with descriptions.

### `static int cmd_aliro(const struct shell *sh, size_t argc, char **argv)`
`modules/woz_uwb/src/shell/aliro_shell.c:242`

Print aliro shell command help: lists all subcommands (status, rx, range, chip, selftest, log,
frames, version) with descriptions.
