# Hardware validation checklist

Automated CI covers the host-side core (KAT suite, coverage floor, ASan/UBSan), but the
end-to-end product runs against a live iPhone, which CI cannot exercise. This checklist is
the manual gate: run every item before cutting a release, and record the results table in
the release notes (see [`RELEASING.md`](RELEASING.md)).

## Test setup

- nRF5340 DK with DWM3000EVB (Arduino header) and X-NUCLEO-NFC12A1, wired per
  [`integration/overlays/dw3000-nfc.overlay`](../integration/overlays/dw3000-nfc.overlay).
- An iPhone (or Apple Watch) with the lock's Aliro key provisioned in Wallet.
- Serial console attached (`make term`) to observe logs.

## Checklist

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

## Recording results

Copy the table into the release notes with a Result column (`pass` / `fail` / `n/a`),
plus: firmware commit hash, NCS version, board revision, phone model, and iOS version.
A release ships only when every row is `pass`.
