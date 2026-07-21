# nRF5340 bring-up

The primary, validated target, from parts on the bench to a healthy first
boot. Toolchain installation is in [set-up.md](set-up.md); this page is the
hardware side and what to check once the image is on the board.

## Parts

| Part | Role |
|---|---|
| nRF5340 DK | Host SoC: BLE + Matter and the ranging engine |
| DWM3000EVB (DW3110) | UWB radio, on the Arduino header (SPIM4) |
| X-NUCLEO-NFC12A1 (ST25R300) | NFC reader front end for tap (SPIM2) |

Pin assignments live in
[`../ports/nrf5340dk/overlays/dw3000-nfc.overlay`](../ports/nrf5340dk/overlays/dw3000-nfc.overlay),
which is the source of truth: if the overlay changes, the wiring must change
with it.

## Before powering

* The DW3000 is a 3.3 V part: power the DWM3000EVB from a 3.3 V rail, never
  5 V, and share a common ground with the DK.
* The DWM3000EVB carries its own **power-select jumper**. If it selects the
  wrong source, SPI fails silently: no valid device ID, a responder that
  never listens. Check it before suspecting software.

## Build, flash, console

With the toolchain installed and the workspace bootstrapped
([set-up.md](set-up.md)):

```bash
make build         # merged image lands in ./build/merged.hex
make flash-erase   # first flash of a net-core image; plain `make flash` after
make term          # console + Zephyr shell, on the DK's VCOM1 (VCOM0 is silent)
```

A net-core configuration change also needs `make flash-erase`; a plain
`make flash` leaves the old net-core image in place.

## First-boot checks

* `make selftest` builds a boot self-test that exercises the radio bring-up
  with no phone present, which isolates a wiring problem from a protocol one.
* On the shell, the `aliro` command group gives a one-glance view: `aliro
  status`, `aliro chip` (reads the DW3110 device ID over SPI), `aliro range`.
* A healthy release boot is clean on the console and starts BLE advertising.

## Prove the unlock

With the lock commissioned into Apple Home and the key in Wallet, the
end-to-end pass criteria live in
[hardware-validation.md](hardware-validation.md): tap on the NFC reader
(Express Mode, screen off), approach unlock with the phone pocketed, and a
walk-away relock that does not oscillate at the boundary.

## If something fails

Symptoms and fixes, including the SPI and ranging failure modes, are in
[troubleshooting.md](troubleshooting.md).
