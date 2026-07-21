# nRF5340 bring-up

The primary, validated target. Parts on the bench to a healthy first boot.
Toolchain install is in [set-up.md](set-up.md); this page is the hardware
side.

## Parts

| Part | Role |
|---|---|
| nRF5340 DK | Host SoC: BLE + Matter and the ranging engine |
| DWM3000EVB (DW3110) | UWB radio, on the Arduino header (SPIM4) |
| X-NUCLEO-NFC12A1 (ST25R300) | NFC reader front end for tap (SPIM2) |

Pin assignments live in
[`../ports/nrf5340dk/overlays/dw3000-nfc.overlay`](../ports/nrf5340dk/overlays/dw3000-nfc.overlay).
That file is the source of truth. If it changes, the wiring changes with it.

## Before powering

* The DW3000 is a 3.3 V part. Never feed it 5 V. Share a common ground with
  the DK.
* The DWM3000EVB has its own **power-select jumper**. Wrong position: SPI
  fails silently, no device ID, a responder that never listens. Check it
  before suspecting software.

## Build, flash, console

```bash
make build
make flash-erase
make term
```

The image lands in `./build/merged.hex`. The first flash needs the erase;
plain `make flash` after. The console is on the DK's VCOM1; VCOM0 is
silent.

A net-core config change also needs `make flash-erase`. A plain `make flash`
keeps the old net-core image.

## First-boot checks

* `make selftest` builds a boot self-test. It exercises the radio with no
  phone present, so it separates a wiring problem from a protocol one.
* On the shell: `aliro status`, `aliro chip` (reads the DW3110 device ID over
  SPI), `aliro range`.
* A healthy release boot is clean on the console and starts BLE advertising.

## Prove the unlock

Commission the lock into Apple Home; the key lands in Wallet. Then run the
pass criteria in [hardware-validation.md](hardware-validation.md): tap
(Express Mode, screen off), approach unlock with the phone pocketed, and a
walk-away relock that does not oscillate.

## If something fails

SPI and ranging failure modes are in
[troubleshooting.md](troubleshooting.md).
