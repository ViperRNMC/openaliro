# openaliro on the nRF5340 DK: flash guide

The primary openaliro target: an Aliro lock with NFC tap and UWB approach unlock. It
joins Apple Home over Thread, your iPhone carries the key in Wallet, and the lock
opens as you walk up or on a tap.

| File | What it is |
|---|---|
| `merged.hex` | application core image (Matter, Aliro, the UWB engine) |
| `merged_CPUNET.hex` | network core image (radio controller) |
| `flash.sh` | flashes both cores over the DK's on-board J-Link |
| `FLASH.md` / `FLASH.html` | this guide, plain text and styled |
| `VERSION.txt` | release tag, commit, and build date |

## 1. What you need

| Part | Role |
|---|---|
| Nordic nRF5340 DK | host board: BLE, Thread, Matter, ranging engine |
| Qorvo DWM3000EVB (DW3110) | UWB radio; seats on the DK's Arduino header |
| ST X-NUCLEO-NFC12A1 (ST25R300) | NFC front end; ribbon-wired (below) |
| 8 jumper wires or a ribbon | NFC board to DK |
| USB cable | power, flashing, serial console |

Apple side: an iPhone with a UWB chip (iOS 26 is the validated floor) and a
Thread-capable Home hub (HomePod or Apple TV 4K) in the same home.

## 2. Install the tools

1. Install the [SEGGER J-Link software](https://www.segger.com/downloads/jlink/): the
   USB driver and programming backend.
2. Download [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util),
   a single executable. macOS/Linux:

   ```bash
   chmod +x nrfutil
   sudo mv nrfutil /usr/local/bin/
   ```

   If macOS blocks it, allow it under System Settings, Privacy & Security. Windows:
   run `nrfutil.exe` from its folder or add it to PATH.
3. Once: `nrfutil install device`
4. Check: `nrfutil device list` (shows the DK once plugged in)

Prefer a GUI? The Programmer app in
[nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop)
flashes both hex files: add the DK, add both files, Erase & write.

You also need a serial terminal: `screen` (built into macOS and most Linux), `minicom`, or PuTTY.

## 3. Assemble

> **Check the DWM3000EVB's power-select jumper first.** Wrong source and the radio
> fails silently (no device ID, a deaf responder), looking exactly like a software
> fault. It cost days on the bench once.

1. Seat the DWM3000EVB on the DK's Arduino header, flat, every pin socketed.
2. Wire the NFC12A1's SPI signals (see its silk screen) to these DK pins, from the
   build's source of truth `ports/nrf5340dk/overlays/dw3000-nfc.overlay`:

| Signal | nRF5340 DK pin |
|---|---|
| SPI SCK | P0.06 |
| SPI MOSI | P0.07 |
| SPI MISO | P0.25 |
| SPI CS | P0.26 |
| IRQ | P1.08 |
| RST | P1.07 |
| 3V3 | 3V3 |
| GND | GND |

The four SPI lines sit on the DK's A2 to A5 positions; the NFC board is wired, not
stacked, since the DWM3000EVB occupies the header.

3. USB into the DK's J-Link connector (next to the power switch), switch On.

## 4. Flash

```bash
bash flash.sh
```

Programs the network core, then the application core, then resets. Both cores are
fully erased first, wiping any previous commissioning. Several DKs? Pass a J-Link
serial: `bash flash.sh 1050012345`.

Manual equivalent:

```bash
nrfutil device program --firmware merged_CPUNET.hex --core network \
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_NONE
nrfutil device program --firmware merged.hex --core application \
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_SYSTEM
```

## 5. Watch it boot

Open the DK's serial console at 115200 baud (the DK enumerates several ports; the console is usually the first):

- **macOS:** `screen /dev/cu.usbmodem<something>1 115200`
- **Linux:** `screen /dev/ttyACM0 115200`
- **Windows:** PuTTY, Serial, the DK's first COM port, 115200

The boot log prints the Matter onboarding QR code URL and manual pairing code; RESET reprints them.

## 6. Add it to Apple Home

1. Open the QR code URL on another screen, or keep the 11-digit code handy.
2. Home app: add accessory, scan the QR code or type the code manually.
3. Keep the phone next to the DK; commissioning starts over BLE, then joins Thread. Expect a minute or two.
4. Home warns about an uncertified accessory (test certificates); add anyway.
5. Home then provisions the key into Wallet on its own. Walk up to unlock, or tap the NFC antenna.

## 7. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `nrfutil device list` shows nothing | Wrong USB connector (use the J-Link one), power switch Off, or J-Link software missing |
| Flash fails or verify errors | Reseat USB, power-cycle, retry; close any other debugger session |
| No boot log | Wrong port among several; try the next, then press RESET |
| No UWB device ID, or approach unlock dead | Power-select jumper (see Assemble) or unseated shield |
| NFC tap does nothing | Re-check the 8-wire map, especially A2 to A5 |
| Home cannot find the accessory | No Thread border router hub, Bluetooth off, or already commissioned (reflash to wipe) |
| Commissioning fails near the end | Thread join: hub online, same home; remove the half-added accessory and retry |
| No key in Wallet | Wait a few minutes; needs iOS 26+ and a UWB iPhone |

More depth, wiring checks, and a radio self-test:
<https://github.com/asxeem/openaliro/blob/main/docs/troubleshooting.md>

## Notes

- Evaluation firmware with Matter test certificates; do not put it in charge of a door you care about.
- Reflashing wipes commissioning; remove the stale accessory from Home before re-adding.
