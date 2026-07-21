# Aliro reader on ESP32-S3 (ESP-IDF)

The full Aliro reader stack — BLE transport, credential authentication, ranging setup,
and the UWB engine — built as ESP-IDF components, with a small bench app on top. The
Matter door lock in [`../matter-lock`](../matter-lock) reuses these components
verbatim; this tree is where they live and where you drive them without Matter in the
way.

**Status: hardware-validated.** The full chain has run on an ESP32-S3 + DWM3000EVB
against a live iPhone: credential auth, M1-M4 ranging setup, and live DS-TWR distance,
ending in an approach unlock.

## Components

| Component | What it is |
|---|---|
| `aliro_ble` | NimBLE transport: `0xFFF2` advertisement, the SPSM/version GATT characteristics, and the L2CAP CoC that carries the transaction. Works standalone or attached to an existing NimBLE host. See its [`SPEC.md`](../../components/aliro_ble/SPEC.md). |
| `aliro_crypto` | The key schedule and secure channels: SHA-256 / HMAC / HKDF / X9.63 in portable C, with an mbedTLS-PSA backend for AES-256-GCM and P-256. |
| `aliro_reader` | The transaction itself: AUTH0 → AUTH1 → EXCHANGE over APDUs, then the M1-M4 ranging setup, plus the reader identity and credential trust store in NVS. |
| `woz_uwb` | The shared engine. `modules/woz_uwb/src` and `deps/dw3000` are compiled straight from the repo against the `woz_port.h` platform contract, with an ESP-IDF DW3000 backend underneath. |

Only one seam is target-specific:

- **`../../components/woz_uwb/port/`** — the ESP-IDF DW3000 platform backend (`dw3000_spi.c`,
  `dw3000_hw.c`) replacing the Zephyr `deps/dw3000/platform/` one (SPI-master + GPIO/IRQ),
  plus `board_pins.h` and a tiny wrap/diag stub (`woz_wrap_stubs.c`).

There is **no Zephyr compatibility layer**. The engine takes its whole OS surface
from `modules/woz_port/include/woz_port.h` (eight functions: heap, monotonic
clock, two sleeps, cycle counter) and its logging from `woz_log.h`, both of which
select an ESP-IDF backend on `ESP_PLATFORM`. The earlier `compat/zephyr/*` shim,
194 lines of fake `<zephyr/*>` headers, was deleted once those two headers
existed. See [`docs/porting.md`](../../../../docs/porting.md).

The CCC STS substitution links the same way it does on Nordic: `--wrap=dwt_rxenable`
(the load-bearing one, where `ccc_shim_rx.c` programs the CCC key and IV on every RX-arm)
alongside `dwt_configurestsiv`, `dwt_configurestsmode`, and `dwt_setcallbacks`.
`verify_port.sh` guards that the seam survives a build.

Two ESP-specific pieces are worth knowing about. SPI runs with **DMA disabled**: a boot
benchmark measured about 84 µs per transaction with DMA, most of it descriptor setup and
cache sync, which dwarfs the bit time for the small register writes on the arm critical
path. And the DW3000 IRQ hands off to a dedicated high-priority task pinned to core 1
that calls `dwt_isr()`, because that call does SPI and cannot run in ISR context.

## Wiring

Source of truth is `../../components/woz_uwb/port/board_pins.h`; the physical
DWM3000EVB-to-header mapping is in
[`docs/esp32-bringup.md`](../../../../docs/esp32-bringup.md).

    SPI2 / FSPI:  SCLK 12   MOSI 11   MISO 13   CS 10
    control:      RSTn  4   IRQ   5   WAKEUP 6
    clock:        2 MHz init / 8 MHz steady

## Build, flash, run

```bash
cd ports/esp32/apps/reader
idf.py set-target esp32s3   # once per checkout
make build
make flash
make monitor
```

`make` alone prints the grouped target list: `build`, `menuconfig`, `size`, `flash`,
`app-flash` (app only, fast iteration), `flash-erase`, `monitor`, `term` (tio), `ports`,
`clean`. ESP-IDF is expected at `~/esp/esp-idf`; override with `IDF_EXPORT=`.

`flash` and `monitor` auto-select the board's USB-UART and **refuse any SEGGER/J-Link
port**, so this can never write to an nRF5340 DK sharing the bench. If another process
holds the port, flashing refuses with a clear message; `FORCE=1` stops the holder first.

## Console

`make monitor` gives a REPL at the `esp32>` prompt:

| Command | What it does |
|---|---|
| `status` | responder state, last and trusted range |
| `range` | latest distance |
| `aliro-start` / `aliro-stop` | start or stop the demo DS-TWR responder (canned URSK, no phone needed) |
| `aliro-prov` | show reader identity and credential trust store |
| `aliro-trust` | persist the last-presented credential as trusted |
| `help`, `clear` | REPL built-ins |

At boot the app brings the radio up, starts the demo responder, starts the reader, and
then polls for a distance every 500 ms. Bringing the radio up at boot rather than on
first use is deliberate: probing the DW3000 from inside a BLE host callback fails.

## Tests

```bash
ports/esp32/test/run.sh
```

Five host suites (no ESP-IDF, no hardware): the port headers, the `aliro_crypto` key
schedule against published vectors, the `aliro_apdu` wire codec, the `aliro_prov`
identity and trust logic, and the bolt-state LED policy. The crypto core compiles
host-identical to target, which is what makes a host KAT a statement about on-target
behavior.

A sixth step, `verify_port.sh`, builds the firmware and checks the link seam, the
excluded diagnostic sources, and that the app still fits its partition. It skips cleanly
with a notice when `idf.py` is not on `PATH`.

These suites are not yet a CI gate. Run them yourself before believing a crypto or wire
change — every bug in the gotchas log built cleanly first.

## Further reading

- [`docs/esp32-bringup.md`](../../../../docs/esp32-bringup.md): wire it up and confirm
  the radio answers.
- [`docs/esp32-gotchas.md`](../../../../docs/esp32-gotchas.md): every trap hit on the
  way to a working unlock, with symptom and fix.
- [`../matter-lock/README.md`](../matter-lock/README.md) — the same stack inside a
  Matter door lock.
