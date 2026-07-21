# `woz_uwb` — the shared engine, on ESP-IDF

This component contains almost no code of its own. Its job is to let
`modules/woz_uwb/src` and `deps/dw3000` — the same sources the nRF5340 build ships —
compile for the ESP32-S3. Everything here is the hardware seam that makes that possible.

Keeping one engine is the point. The shared sources are the layer the nRF has already
proven correct; forking them for ESP32 would mean two engines to keep honest.

## The API seam lives in the engine, not here

There is no `<zephyr/*>` compatibility tree any more. The engine takes its whole OS
surface from `modules/woz_port/include/woz_port.h`, eight functions (heap, monotonic
clock, two sleeps, cycle counter), and its logging from `woz_log.h`. Both select an
ESP-IDF backend on `ESP_PLATFORM`: FreeRTOS, `esp_timer`, `esp_rom_delay_us`,
`esp_cpu_get_cycle_count`, and `esp_log`. The pure byte and bit helpers are platform-free
and sit alongside them in `woz_bytes.h` / `woz_util.h`.

`../../test/test_port_headers.c` exercises those headers on the host, and
[`docs/porting.md`](../../../../docs/porting.md) describes what a new target owes them.

## `port/` — the hardware seam

The ESP-IDF DW3000 backend, replacing `deps/dw3000/platform/`:

- **`dw3000_spi.c`** — SPI master with chip select driven as a plain GPIO, held low across
  a whole DW3000 command. **DMA is disabled on purpose.** A boot benchmark measured about
  84 µs per transaction with DMA, nearly all of it descriptor setup and cache sync, which
  dwarfs the bit time for the small register writes on the arming critical path. Clock
  speed is not the lever: 2 MHz versus 8 MHz was about 75 versus 84 µs. Transfers larger
  than the hardware buffer are chunked under one chip-select window, which the DW3000
  streams through sequentially.
- **`dw3000_hw.c`** — GPIO reset, wakeup, and the interrupt path. The ISR only gives a
  semaphore; a dedicated high-priority task pinned to core 1 calls `dwt_isr()`, because
  that call performs SPI and cannot run in interrupt context.
- **`board_pins.h`** — the pin map. Source of truth; the wiring table in
  [`docs/esp32-bringup.md`](../../../../docs/esp32-bringup.md) mirrors it.
- **`woz_wrap_stubs.c`** — the minimal RX-callback chain that the excluded diagnostic
  sources would otherwise have provided. Without it the responder receives frames but
  never replies.

## Configuration and link seam

Engine layers are selected as compile definitions rather than Kconfig, mirroring what the
in-tree `CMakeLists.txt` gates on. Diagnostic and Nordic-specific sources are excluded.

The STS substitution links exactly as it does on Nordic, through `--wrap`. The
load-bearing one is `dwt_rxenable`, where the CCC key and IV are programmed on every
RX-arm. `../../test/verify_port.sh` checks after each build that the wrap flags are still
in the link, that the wrapper symbols are defined, that the engine still references the
wrapped symbol, and that the excluded sources stayed out.

Warnings are relaxed for this component only (`-Wno-format`,
`-Wno-maybe-uninitialized`), because ESP-IDF builds with `-Werror` and the shared sources
were written where `int32_t` is `int`. This changes no behavior, and it applies to no
other component.
