# Hardware validation checklist

Automated CI covers the host-side core of the nRF target (KAT suite, coverage floor,
ASan/UBSan). It does not build either ESP32 port, and the end-to-end product runs against
a live iPhone, which CI cannot exercise at all. This checklist is the manual gate: run
every applicable item before cutting a release, and record the results table in the
release notes (see [`RELEASING.md`](RELEASING.md)).

Two targets ship from this repository, so there are two checklists. A release covering
only one target runs only that target's rows and records the other as `n/a`.

## nRF5340 DK

### Test setup

- nRF5340 DK with DWM3000EVB (Arduino header) and X-NUCLEO-NFC12A1, wired per
  [`ports/nrf5340dk/overlays/dw3000-nfc.overlay`](../ports/nrf5340dk/overlays/dw3000-nfc.overlay).
- An iPhone (or Apple Watch) with the lock's Aliro key provisioned in Wallet.
- Serial console attached (`make term`) to observe logs.

### Checklist

| ID | Procedure | Pass criterion |
|---|---|---|
| HV-1 | `make test` on the release commit | Exit 0, all host KATs pass |
| HV-2 | `make rebuild` (pristine) | Exit 0, image links and fits flash |
| HV-3 | Flash a `make selftest` build, boot with no phone present | Boot self-test reports pass on the console |
| HV-4 | Flash the release image (`make flash-erase` for a first flash), boot | Clean boot, no errors on the console, BLE advertising starts |
| HV-5 | Tap the phone on the NFC reader (Express Mode, screen off) | Lock actuates to unlocked; console logs the granted access |
| HV-6 | Relock, then approach from well outside ranging distance, phone pocketed | Lock unlocks on approach with no phone interaction |
| HV-7 | Walk away from the lock | Lock relocks after passing the hysteresis margin, and does not oscillate at the boundary |
| HV-8 | Power-cycle the DK, wait for boot, repeat HV-5 and HV-6 | Both unlock paths work without re-provisioning the key |

## ESP32-S3

No NFC tap path exists on this target, so there is no equivalent of HV-5.

### Test setup

- ESP32-S3 dev board with a DWM3000EVB wired per
  [`docs/esp32-bringup.md`](esp32-bringup.md), including the EVB's
  power-select jumper.
- An iPhone with a key provisioned in Wallet for *this* reader identity. A key minted
  against a different reader will not authenticate.
- Serial console attached (`make monitor` from `ports/esp32-matter`).

### Checklist

| ID | Procedure | Pass criterion |
|---|---|---|
| EV-1 | `ports/esp32-idf/test/run.sh` on the release commit | Exit 0, all host suites pass |
| EV-2 | `make rebuild` in `ports/esp32-matter` | Exit 0; `verify_port.sh` reports the link seam intact and the app fits its partition |
| EV-3 | `make flash-erase`, then boot | Clean boot, onboarding codes printed, no watchdog resets |
| EV-4 | Commission into a home with the printed code | Commissioning completes; `status` shows the fabric |
| EV-5 | Confirm a key lands in the phone's wallet | Key appears, tied to this reader |
| EV-6 | `aliro prov` on the console | Reports a provisioned identity, not the dev-identity fallback warning |
| EV-7 | Approach from well outside ranging distance, phone pocketed | Wallet unlock animation plays and the bolt opens, with no phone interaction |
| EV-8 | Watch the console through EV-7 | Continuous positive distances tracking the approach; no watchdog reset |
| EV-9 | Walk away | Bolt relocks past the hysteresis margin and does not oscillate at the boundary |
| EV-10 | Re-approach within the same session | Unlocks again without a reconnect |
| EV-11 | Power-cycle the board, wait for boot, repeat EV-7 | Unlock works without re-commissioning or re-provisioning |

EV-7 is the row that matters most and the one most easily faked: the bolt moving is not
a pass. The Wallet animation is the pass criterion, because that is what proves the
reader told the phone it granted access rather than just actuating locally.

## Recording results

Copy the relevant tables into the release notes with a Result column (`pass` / `fail` /
`n/a`), plus: firmware commit hash, toolchain version (NCS, or ESP-IDF and esp-matter),
board revision, phone model, and iOS version. A release ships only when every applicable
row is `pass`.
