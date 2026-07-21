# openaliro on the ESP32-S3: flash guide

The complete Aliro Matter lock on a single ESP32-S3: it commissions into Apple Home
over Wi-Fi, your iPhone carries the key in Wallet, and the lock opens as you walk up
(BLE + UWB approach unlock; no NFC tap on this target).

| File | What it is |
|---|---|
| `openaliro-matter-lock.bin` | one merged image (bootloader + partitions + app), flashed at 0x0 |
| `flash.sh` | flashes the image with esptool |
| `FLASH.md` / `FLASH.html` | this guide, plain text and styled |
| `VERSION.txt` | release tag, commit, and build date |

## 1. What you need

| Part | Role |
|---|---|
| ESP32-S3 dev board | host: Wi-Fi, Matter, BLE, ranging engine. Validated on ESP32-S3-WROOM N16R8 (16 MB flash, 8 MB PSRAM); smaller variants untested |
| Qorvo DWM3000EVB (DW3110) | UWB radio, hand-wired over SPI (11 connections) |
| 11 jumper wires | the SPI wiring |
| USB cable | power, flashing, serial console; must be a data cable |

Apple side: an iPhone with a UWB chip (iOS 26 is the validated floor), a Home hub
(HomePod or Apple TV), and a 2.4 GHz Wi-Fi network (the ESP32 has no 5 GHz).

## 2. Install the tools

1. Python 3 (`python3 --version` to check; on Windows from
   [python.org](https://www.python.org/downloads/) with "Add to PATH" ticked).
2. `pip install esptool` (`pip3` on some systems, `py -m pip` on Windows).
3. A serial terminal: `screen` (built into macOS and most Linux), `minicom`, or PuTTY.

Boards using the S3's native USB need no driver; boards with a CP210x or CH340 bridge
may need that vendor's driver on Windows and older macOS.

## 3. Wire it

> **Check the DWM3000EVB's power-select jumper first.** Wrong source and the radio
> fails silently (no device ID, a deaf responder), looking exactly like a software
> fault. It cost days on the bench once.

Power the EVB from **3V3**, not 5 V (the DW3000 is a 3.3 V part), share ground, and
wire per the build's source of truth `ports/esp32/components/woz_uwb/port/board_pins.h`:

| DWM3000EVB (Arduino pin) | Signal | ESP32-S3 |
|---|---|---|
| D13 | SCLK | GPIO12 |
| D11 | MOSI | GPIO11 |
| D12 | MISO | GPIO13 |
| D10 | CS | GPIO10 |
| D8 | IRQ | GPIO5 |
| D7 | RSTn | GPIO4 |
| D9 | WAKEUP | GPIO6 |
| 3V3 | power | 3V3 |
| GND | ground | GND |
| D1 | SPI-POL | GND (mode-0 strap) |
| D0 | SPI-PHA | GND (mode-0 strap) |

The two straps hold the DW3000 in SPI mode 0; skip them if your EVB revision already fixes the mode (check its manual).

## 4. Flash

```bash
bash flash.sh
```

esptool auto-detects the port; to name one, `bash flash.sh /dev/ttyACM0`
(`/dev/cu.usbmodem…` on macOS, `COM…` on Windows). Chip not detected? Hold BOOT while
plugging in, retry, then replug.

Manual equivalent:

```bash
esptool.py --chip esp32s3 --baud 460800 write_flash 0x0 openaliro-matter-lock.bin
```

Reflashing wipes any previous commissioning.

## 5. Watch it boot

Open the serial console at 115200 baud (after a BOOT-button flash, replug first):

- **macOS:** `screen /dev/cu.usbmodem<something> 115200`
- **Linux:** `screen /dev/ttyACM0 115200` (`/dev/ttyUSB0` on bridge boards)
- **Windows:** PuTTY, Serial, the board's COM port, 115200

The boot log prints the Matter onboarding QR code URL and manual pairing code, then a
`matter>` prompt; `codes` reprints both.

## 6. Add it to Apple Home

1. Open the QR code URL on another screen, or keep the 11-digit code handy.
2. Home app: add accessory, scan the QR code or type the code manually.
3. Keep the phone next to the board; commissioning starts over BLE, then joins your
   2.4 GHz Wi-Fi. Expect a minute or two.
4. Home warns about an uncertified accessory (test certificates); add anyway.
5. Home then provisions the key into Wallet on its own. Walk up: the Wallet animation
   plays, the lock opens, and it relocks as you leave.

The `matter>` console also has `status`, `range`, `lock`/`unlock`, and `factoryreset`.

## 7. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| esptool "Failed to connect" or no port | Charge-only cable, wrong port, or hold BOOT while plugging in, then replug |
| No boot log | Wrong port, or still in download mode: replug |
| No valid UWB device ID | Power-select jumper (see Wire it), 5 V instead of 3V3, or a wrong SPI line |
| `range` shows nothing with the phone near | UWB wiring, or no key yet: finish commissioning, check Wallet |
| Home cannot find the accessory | No hub, Bluetooth off, or already commissioned (reflash or `factoryreset`) |
| Commissioning fails near the end | Wi-Fi join: 2.4 GHz required; remove the half-added accessory and retry |
| No key in Wallet | Wait a few minutes; needs iOS 26+ and a UWB iPhone |

More depth: the
[bring-up checklist](https://github.com/asxeem/openaliro/blob/main/docs/esp32-bringup.md)
and
[forty-odd known traps](https://github.com/asxeem/openaliro/blob/main/docs/esp32-gotchas.md).

## Notes

- Evaluation firmware with Matter test certificates; do not put it in charge of a door you care about.
- Reflashing wipes commissioning; remove the stale accessory from Home before re-adding.
