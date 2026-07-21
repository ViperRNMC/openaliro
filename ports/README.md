# Ports

Aliro locks on targets other than the primary nRF5340 DK build at the repository root.
Each port reuses `modules/woz_uwb/src` and `deps/dw3000` and keeps all target-specific
code inside its own directory.

The platform contract is [`modules/woz_uwb/src/facade/woz_port.h`](../modules/woz_uwb/src/facade/woz_port.h):
eight functions (heap, monotonic clock, two sleeps, cycle counter), plus `woz_log.h` for
logging. A new RTOS is a new branch in those two headers; a new board on an existing RTOS
is a DW3000 SPI/GPIO backend. See [`docs/porting.md`](../docs/porting.md) for the tiers and
what each costs.

| Directory | What it is | Status |
|---|---|---|
| [`esp32-matter/`](esp32-matter/) | The complete ESP32-S3 lock: a Matter door lock that commissions into a home, provisions a key into the phone's wallet, and unlocks on approach over BLE + UWB | **Hardware-validated.** Approach unlock driven end to end against a live iPhone, Wallet animation and all |
| [`esp32-idf/`](esp32-idf/) | The reader stack the Matter app is built from — BLE transport, credential auth, ranging setup, and the shared UWB engine as ESP-IDF components — plus a bench app to drive them without Matter | **Hardware-validated.** Credential auth, M1-M4 setup, and live DS-TWR distance on silicon |

An early Zephyr-based ESP32-S3 spike (`ports/esp32s3/`, never run on silicon) was
removed; its pin map lives on in [`docs/esp32-bringup.md`](../docs/esp32-bringup.md).
For archaeology, the last commit carrying it is `b11549d`.

## The ESP32-S3 port

Start at [`esp32-matter/README.md`](esp32-matter/README.md) for the whole lock, or
[`esp32-idf/README.md`](esp32-idf/README.md) for the component stack. The two share
every component: `esp32-matter` adds `../esp32-idf/components` to its component path
rather than duplicating anything.

What makes it more than a recompile: the reference design hands the credential
authentication and the ranging key derivation to a closed vendor library that only exists
as an ARM binary. It cannot be linked on the Xtensa S3, so that layer had to be
reimplemented from scratch — the key schedule, the secure channels, the wire codec, and
the reader identity. Everything below the ranging key was already open in
`modules/woz_uwb` and compiles for the S3 unchanged.

Two documents carry the detail:

- [`docs/esp32-gotchas.md`](../docs/esp32-gotchas.md) — every trap hit during bring-up,
  with symptom, cause, and fix. Read it before debugging anything on this target.
- [`../docs/porting-esp32.md`](../docs/porting-esp32.md) — how the port was planned and
  how it actually went.

## The primary target

The nRF5340 DK build lives at the repository root and is hardware-validated end to end,
including the NFC tap path that the ESP32 ports do not have. See the top-level
[README](../README.md).
