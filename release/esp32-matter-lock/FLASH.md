# openaliro on the ESP32-S3: flash guide

This bundle is the complete Aliro Matter lock on a single ESP32-S3: it
commissions into Apple Home over Wi-Fi, a key is provisioned into your iPhone's
Wallet, and the lock opens as you walk up (BLE + UWB approach unlock; there is
no NFC tap on this target).

In this bundle:

| File | What it is |
|---|---|
| `openaliro-matter-lock.bin` | one merged image (bootloader + partition table + app), flashed at offset 0x0 |
| `flash.sh` | flashes the image with esptool |
| `FLASH.md` / `FLASH.html` | this guide, as plain text and as a styled page for your browser |
| `VERSION.txt` | release tag, commit, and build date |

## 1. What you need

Hardware:

| Part | Role |
|---|---|
| ESP32-S3 dev board | host: Wi-Fi, Matter, BLE, and the ranging engine. Validated on an ESP32-S3-WROOM N16R8 module (16 MB flash, 8 MB PSRAM); smaller variants are untested |
| Qorvo DWM3000EVB (DW3110) | UWB radio, hand-wired over SPI (11 connections, below) |
| 11 jumper wires | the SPI wiring |
| USB cable | powers the board, flashes it, and carries the serial console. Must be a data cable, not charge-only |

Apple side:

- An iPhone with a UWB chip. iOS 26 is the validated floor.
- An Apple Home hub (HomePod or Apple TV), and a 2.4 GHz Wi-Fi network for the
  lock to join (the ESP32 does not do 5 GHz).

## 2. Install the tools

All you need on your computer (macOS, Linux, or Windows) is Python 3 and
esptool, plus a serial terminal.

1. Install Python 3 if you do not have it (`python3 --version` to check; on
   Windows, from [python.org](https://www.python.org/downloads/) with "Add to
   PATH" ticked).
2. Install esptool:

   ```bash
   pip install esptool
   ```

   (`pip3` on some systems, `py -m pip install esptool` on Windows.)
3. A serial terminal for the first-run step: `screen` (already on macOS and
   most Linux), `minicom`, or PuTTY on Windows.

Driver note: boards that use the S3's native USB need no driver on a current
OS. Boards with a separate USB-serial bridge chip (CP210x or CH340) may need
that vendor's driver on Windows and older macOS.

## 3. Wire it

> **Before anything else: check the DWM3000EVB's power-select jumper.** If it
> selects the wrong source the radio fails silently: no valid device ID, a
> responder that never listens, and it looks exactly like a software fault.
> This cost days on the bench once. Check it against the EVB manual first.

Power the EVB from the ESP32-S3 board's **3V3** pin, not 5 V (the DW3000 is a
3.3 V part), and share a common ground. The pin map below is transcribed from
the build's source of truth, `ports/esp32/components/woz_uwb/port/board_pins.h`
in the repository:

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

The two straps hold the DW3000 in SPI mode 0; check the EVB manual before tying
them if your EVB revision already fixes the mode on the shield.

## 4. Flash

Plug the board in over USB, then:

```bash
bash flash.sh
```

esptool auto-detects the port; to name one, `bash flash.sh /dev/ttyACM0` (or
`/dev/cu.usbmodem…` on macOS, `COM…` on Windows). If the chip is not detected,
hold the board's BOOT button while plugging the cable in, then retry.

Manual equivalent (what the script runs):

```bash
esptool.py --chip esp32s3 --baud 460800 write_flash 0x0 openaliro-matter-lock.bin
```

Reflashing wipes any previous commissioning; that is the expected clean-install
behavior.

## 5. Watch it boot

Open the board's serial console at 115200 baud. If you flashed through a BOOT
button hold, unplug and replug first so the chip leaves the download mode.

- **macOS:** `screen /dev/cu.usbmodem<something> 115200` (tab-complete;
  `ls /dev/cu.usb*` lists candidates)
- **Linux:** `screen /dev/ttyACM0 115200` (or `/dev/ttyUSB0` on bridge-chip
  boards)
- **Windows:** PuTTY, connection type Serial, the board's COM port, speed
  115200

The boot log prints the Matter onboarding QR code URL and the manual pairing
code, and ends at a `matter>` prompt. Typing `codes` there reprints both.

## 6. Add it to Apple Home

1. Open the QR code URL from the boot log on any other screen (it renders the
   QR code in the browser), or keep the 11-digit manual pairing code handy.
2. In the Home app on the iPhone: add an accessory, then scan the QR code, or
   choose the manual option and type the pairing code.
3. Keep the phone next to the board during commissioning (it starts over BLE
   before the lock joins your 2.4 GHz Wi-Fi). Expect it to take a minute or
   two.
4. The firmware carries Matter test certificates, so Home warns about an
   uncertified accessory; choose to add anyway. That warning is expected for
   this evaluation build.
5. Once commissioned, Home provisions the key into Wallet on its own; there is
   nothing to tap or approve for this step. Then walk up with the phone: the
   Wallet animation plays, the lock opens, and it relocks as you leave.

The `matter>` console also has `status`, `range`, `lock`/`unlock`, and
`factoryreset`.

## 7. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| esptool: "Failed to connect" or no port found | Charge-only USB cable, wrong port, or the chip needs download mode: hold BOOT while plugging in, retry, then replug to boot normally |
| No boot log on the serial port | Wrong port (`ls /dev/cu.usb*` or Device Manager), or the chip is still in download mode: unplug and replug |
| Boot log reports no valid UWB device ID | The DWM3000EVB power-select jumper (see Wire it), 5 V instead of 3V3, or a wrong SPI line; re-check all 11 rows |
| `range` shows nothing when the phone is near | UWB wiring again, or the phone has no key yet: finish commissioning first and check Wallet |
| Home cannot find the accessory | Home hub missing, phone Bluetooth off, or the lock was already commissioned (reflash or `factoryreset` to wipe) |
| Commissioning fails near the end | Usually the Wi-Fi join: 2.4 GHz network required; then remove the half-added accessory and retry |
| No key appears in Wallet | Give it a few minutes after commissioning; confirm the iPhone runs iOS 26 or later and has a UWB chip |

More depth:

- Bring-up checklist and what good console output looks like:
  <https://github.com/asxeem/openaliro/blob/main/docs/esp32-bringup.md>
- Forty-odd known traps with symptoms and fixes:
  <https://github.com/asxeem/openaliro/blob/main/docs/esp32-gotchas.md>

## Notes

- This is evaluation firmware with Matter test certificates. Do not put it in
  charge of a door you care about.
- Reflashing wipes the previous commissioning; remove the stale accessory from
  the Home app before re-adding.
