# Ports

Every target lives here, one directory per platform. Each port reuses the
platform-neutral engine in [`modules/`](../modules) and the vendor driver in
[`deps/dw3000`](../deps/dw3000), and keeps all target-specific code inside its
own directory.

The platform contract is [`modules/woz_port/include/woz_port.h`](../modules/woz_port/include/woz_port.h):
eight functions (heap, monotonic clock, two sleeps, cycle counter, a mutex), plus
`woz_log.h` for logging. A new RTOS is a new branch in those two headers; a new board
on an existing RTOS is a DW3000 SPI/GPIO backend. See [`docs/porting.md`](../docs/porting.md)
for the tiers and what each costs.

| Directory | Target | What it is | Status |
|---|---|---|---|
| *(repository root)* | **nRF5340 DK** | The primary build: NFC tap + UWB approach unlock on top of the Nordic door-lock add-on, assembled by `make bootstrap` from patches and overlays in [`integration/`](../integration) | **Hardware-validated** end to end, including the NFC tap path |
| [`esp32/`](esp32/) | **ESP32-S3** | The complete ESP-IDF port: shared components plus two apps (a Matter door lock and a standalone bench reader) | **Hardware-validated.** Approach unlock driven end to end against a live iPhone, Wallet animation and all |

## The ESP32-S3 port (`esp32/`)

One platform directory, two apps over one set of shared components:

```
esp32/
├── components/          # the stack, as ESP-IDF components:
│   ├── woz_uwb/         #   shared UWB engine + ESP-IDF DW3000 backend (SPI/GPIO/pins)
│   ├── aliro_crypto/    #   credential auth + ranging-key derivation (mbedTLS)
│   ├── aliro_reader/    #   reader, APDU codec, NVS-backed provisioning
│   └── aliro_ble/       #   NimBLE transport
├── apps/
│   ├── matter-lock/     # the full lock: Matter commissioning + Wallet provisioning
│   └── reader/          # standalone bench app: drives the stack without Matter
└── test/                # host-runnable port tests (no ESP-IDF needed): test/run.sh
```

Start at [`esp32/apps/matter-lock/README.md`](esp32/apps/matter-lock/README.md) for the
whole lock, or [`esp32/apps/reader/README.md`](esp32/apps/reader/README.md) for the
component stack and the bench app. Both apps consume `esp32/components/` — nothing is
duplicated between them.

What makes it more than a recompile: the reference design hands the credential
authentication and the ranging key derivation to a closed vendor library that only exists
as an ARM binary. It cannot be linked on the Xtensa S3, so that layer had to be
reimplemented from scratch — the key schedule, the secure channels, the wire codec, and
the reader identity. Everything below the ranging key was already open in
`modules/woz_uwb` and compiles for the S3 unchanged.

Two documents carry the detail:

- [`docs/esp32-gotchas.md`](../docs/esp32-gotchas.md) — every trap hit during bring-up,
  with symptom, cause, and fix. Read it before debugging anything on this target.
- [`docs/porting-esp32.md`](../docs/porting-esp32.md) — how the port was planned and
  how it actually went.

An early Zephyr-based ESP32-S3 spike (`ports/esp32s3/`, never run on silicon) was
removed; its pin map lives on in [`docs/esp32-bringup.md`](../docs/esp32-bringup.md).
For archaeology, the last commit carrying it is `b11549d`.

## The primary target

The nRF5340 DK build is assembled at the repository root: the app itself is Nordic's
door-lock add-on, fetched pristine by `make bootstrap` and patched from
[`ports/nrf5340dk/patches/`](../ports/nrf5340dk/patches), configured by
[`ports/nrf5340dk/overlays/`](../ports/nrf5340dk/overlays), with the engine supplied from
`modules/` via `ZEPHYR_EXTRA_MODULES`. See the top-level [README](../README.md).
