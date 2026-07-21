# woz_port — the platform contract

Two headers, no sources. This is the entire surface a new platform must provide
to run the UWB engine and the Aliro reader:

- [`include/woz_port.h`](include/woz_port.h) — eight functions (heap, monotonic
  clock, two sleeps, a cycle counter) plus one blocking mutex. Each backend is a
  `#if` branch of small static inlines; the existing three are Zephyr
  (`__ZEPHYR__`), ESP-IDF (`ESP_PLATFORM`), and the host test build
  (`WOZ_PORT_HOST`). No backend defined is a compile error, never a guess.
- [`include/woz_log.h`](include/woz_log.h) — Zephyr's `LOG_*` spellings, mapped
  onto the platform logger (`ESP_LOG*` on ESP-IDF, no-ops on the host build), plus
  `woz_printf`.

Porting to a new RTOS starts here: add one branch to each header (about 55 lines
total), then supply a DW3000 SPI/GPIO backend. The full recipe, including what is
deliberately NOT in this contract and why, is in
[`docs/porting.md`](../../docs/porting.md); the header comments in `woz_port.h`
state the boundary rules.

Consumed by `modules/woz_uwb`, `modules/woz_aliro`, every port under
[`ports/`](../../ports), and the host test suite in
[`tests/host`](../../tests/host).
