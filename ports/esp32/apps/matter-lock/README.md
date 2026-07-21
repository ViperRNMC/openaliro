# Aliro Matter door lock — ESP32-S3

A complete Aliro lock on a single ESP32-S3: it commissions into a Matter home, has a key
provisioned into the phone's wallet, and then unlocks on approach over BLE + UWB. This is
the port's front door — the standalone bench app in [`../reader`](../reader) is the
same reader without Matter around it.

**Status: hardware-validated.** Approach unlock has been driven end to end on an
ESP32-S3 + DWM3000EVB against a live iPhone: the Wallet unlock animation plays as you
walk up, the bolt opens, and it relocks on departure.

## What runs where

```
ESP32-S3
├─ esp-matter / connectedhomeip ....... commissioning, Door Lock cluster, OTA
│   └─ aliro_reader_delegate .......... the cluster's Aliro half: reader identity in,
│                                        provisioning out to Wallet
├─ aliro_ble (NimBLE) ................. 0xFFF2 advertisement, GATT, L2CAP CoC
├─ aliro_reader ....................... AUTH0 → AUTH1 → EXCHANGE, then M1-M4
├─ aliro_crypto ....................... key schedule, secure channel (mbedTLS-PSA)
├─ modules/woz_uwb (shared) ........... CCC key ladder, MAC, STS, DS-TWR responder
└─ DW3000 backend ..................... ESP-IDF SPI + GPIO/IRQ
```

Everything below the delegate is shared verbatim with `../reader`: this app's
`CMakeLists.txt` adds `../../components` to `EXTRA_COMPONENT_DIRS` rather than
duplicating any of it. The UWB engine (`modules/woz_uwb`) is in turn shared verbatim with
the nRF5340 build at the repository root.

## Prerequisites

- ESP-IDF and esp-matter, installed and exportable. The Makefile expects them at
  `~/esp/esp-idf` and `~/esp/esp-matter`; override with `IDF_EXPORT=` and
  `ESP_MATTER_PATH=` on any target.
- ESP32-S3 dev board plus a DWM3000EVB wired per
  [`docs/esp32-bringup.md`](../../../docs/esp32-bringup.md).
- An Apple Home setup that can commission a Matter-over-Wi-Fi accessory and mint an Aliro
  key: a home hub, and an iPhone new enough to carry the key.

## Build, flash, run

```bash
cd ports/esp32-matter
make set-target      # once per checkout (TARGET=esp32s3)
make env             # sanity-check the toolchain without building
make go              # build + flash + monitor, the usual bench loop
```

`make` alone prints the grouped target list. The rest: `build`, `rebuild`, `reconfigure`,
`menuconfig`, `size`, `flash`, `app-flash` (app only, fast iteration), `flash-erase`
(full chip erase — drops commissioning **and** the Aliro NVS blob), `monitor`, `term`
(tio, no backtrace decode), `ports`, `clean`.

Serial-port handling is deliberate, not incidental. `flash`/`monitor` auto-select the
board's USB-UART and **refuse any SEGGER/J-Link port**, so this can never write to an
nRF5340 DK sharing the bench. If another process holds the port, flashing refuses with a
clear message; `FORCE=1` stops the holder first.

## Console

`make monitor` gives a REPL at the `matter>` prompt:

| Command | What it does |
|---|---|
| `status` | bolt state, fabric count, last and trusted range |
| `range` | latest distance |
| `lock` / `unlock` | drive the bolt directly |
| `codes` | reprint the commissioning QR URL and manual pairing code |
| `aliro prov` | show reader identity and credential trust store |
| `aliro trust` | persist the last-presented credential as trusted |
| `factoryreset` | erase all Matter state and reboot |
| `help`, `clear` | REPL built-ins |

## Commissioning and provisioning

1. Flash and boot. The onboarding codes print at boot, and `codes` reprints them.
2. Commission the lock into your home with the QR or manual code.
3. The controller writes the reader configuration to the Door Lock cluster
   (`SetAliroReaderConfig`). The delegate persists that identity through `aliro_prov` into
   NVS and refreshes the BLE advertisement, so the identity survives reboots.
4. The home app mints a key into the phone's wallet against that reader identity.
5. Walk up. The reader authenticates over L2CAP, derives the ranging key, negotiates
   M1-M4, and ranges over UWB.

Unlock policy is proximity-driven, not timer-driven: unlock at or under 100 cm, hold
while the peer is present, relock past 150 cm or when the ranging session drops. The
hysteresis band stops boundary flapping. CHIP's fixed `AutoRelockTime` is set to 0
because a fixed timer fundamentally fights approach unlock — it would relock while the
phone is still standing there.

The onboard WS2812 on GPIO48 shows bolt state: dark when locked, blue when opened by
Aliro, green when opened any other way.

## Configuration worth knowing

Set in `sdkconfig.defaults`; each of these is load-bearing, not a default that happened
to stick:

| Setting | Why |
|---|---|
| `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=16384` | the 4096 default overflows during software P-256 |
| `CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM=2` | the Aliro transaction needs a CoC channel |
| `CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING=n` | BLE must stay up after commissioning, or re-init crashes the reader |
| `CONFIG_ENABLE_CHIP_SHELL=n` | this app's own REPL owns the console UART |
| `CONFIG_ENABLE_ALIRO_BLE_UWB=y` | advertises the cluster's Aliro features and registers the delegate |

The reader attaches to esp-matter's existing NimBLE host rather than starting a second
BLE stack; the Aliro GATT service is registered through `ConfigureExtraServices` before
Matter starts.

## Caveats

- The reader falls back to a fixed, non-secret **dev identity** with a dev-open trust
  policy when nothing has been provisioned. It accepts the presented credential and logs
  a warning. That is a bench seam where real issuer-chain validation belongs, not a
  security control.
- No vendor ID, product ID, or passcode is pinned in this repository; the build inherits
  esp-matter's test defaults. Anything beyond a bench setup needs its own.
- Matter over Wi-Fi on the S3 is not low-power. Fine for a mains-powered reader, wrong
  for a battery lock.

## Further reading

- [`docs/esp32-gotchas.md`](../../../docs/esp32-gotchas.md) — every trap hit during this
  bring-up, with symptom and fix. Read this before debugging anything.
- [`../reader/README.md`](../reader/README.md) — the shared component stack.
- [`../../../docs/porting-esp32.md`](../../../docs/porting-esp32.md) — how the port was planned
  and how it actually went.
