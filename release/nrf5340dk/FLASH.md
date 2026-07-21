# openaliro on the nRF5340 DK: flash guide

This bundle is the primary openaliro target: an Aliro lock with NFC tap and UWB
approach unlock, built on the Nordic door-lock add-on. It joins Apple Home as a
Matter-over-Thread accessory; your iPhone then carries the key in Wallet and the
lock opens as you walk up, or on an NFC tap.

In this bundle:

| File | What it is |
|---|---|
| `merged.hex` | application core image (Matter, Aliro, the UWB engine) |
| `merged_CPUNET.hex` | network core image (radio controller) |
| `flash.sh` | flashes both cores over the DK's on-board J-Link |
| `FLASH.md` / `FLASH.html` | this guide, as plain text and as a styled page for your browser |
| `VERSION.txt` | release tag, commit, and build date |

## 1. What you need

Hardware:

| Part | Role |
|---|---|
| Nordic nRF5340 DK | host board: BLE, Thread, Matter, and the ranging engine |
| Qorvo DWM3000EVB (DW3110) | UWB radio; seats directly on the DK's Arduino header |
| ST X-NUCLEO-NFC12A1 (ST25R300) | NFC reader front end for tap; ribbon-wired (below) |
| 8 jumper wires or a ribbon | connects the NFC board to the DK |
| USB cable | powers the DK, flashes it, and carries the serial console |

Apple side:

- An iPhone with a UWB chip. iOS 26 is the validated floor.
- An Apple Home hub that can act as a Thread border router (HomePod or
  Apple TV 4K), set up in the same home, since the lock joins over Thread.

## 2. Install the tools

You need two things on your computer (macOS, Linux, or Windows): Nordic's
`nrfutil` and the SEGGER J-Link software it drives.

1. Install the
   [SEGGER J-Link software](https://www.segger.com/downloads/jlink/) for your
   OS (pick the "J-Link Software and Documentation Pack"). This provides the
   USB driver and programming backend.
2. Download [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util)
   (a single executable, no installer). Then:
   - **macOS / Linux:** make it executable and put it on your PATH:

     ```bash
     chmod +x nrfutil
     sudo mv nrfutil /usr/local/bin/
     ```

     If macOS blocks it as being from an unidentified developer, allow it under
     System Settings, Privacy & Security.
   - **Windows:** put `nrfutil.exe` somewhere convenient and run it from that
     folder (or add the folder to PATH).
3. Install the device command, once:

   ```bash
   nrfutil install device
   ```

4. Check it sees tools and (once plugged in) the DK:

   ```bash
   nrfutil device list
   ```

Prefer a GUI? The Programmer app in
[nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop)
can flash both hex files instead of `flash.sh`: add the DK, add both files, and
use Erase & write.

You will also want a serial terminal for the first-run step: `screen` (already
on macOS and most Linux), `minicom`, or PuTTY on Windows.

## 3. Assemble

> **Before anything else: check the DWM3000EVB's power-select jumper.** If it
> selects the wrong source the radio fails silently: no valid device ID, a
> responder that never listens, and it looks exactly like a software fault.
> This cost days on the bench once. Check it against the EVB manual first.

1. Seat the DWM3000EVB shield on the DK's Arduino header. It only fits one way;
   make sure every pin is in a socket and the shield sits flat.
2. Wire the NFC12A1 to the DK. The pin map below is transcribed from the build's
   source of truth, `ports/nrf5340dk/overlays/dw3000-nfc.overlay` in the
   repository; connect the matching SPI signals on the NFC12A1 (see its silk
   screen or schematic) to these DK pins:

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

The four SPI lines sit on the DK's A2 to A5 analog header positions. The NFC
board is wired, not stacked: the DWM3000EVB occupies the header.

3. Connect the USB cable to the DK's J-Link USB connector (the one next to the
   power switch) and slide the power switch to On.

## 4. Flash

With the DK plugged in and on:

```bash
bash flash.sh
```

It programs the network core, then the application core, then resets the board.
Both cores are fully erased first, so any previous commissioning is wiped; this
is the expected clean-install behavior. To pick one DK among several, pass its
J-Link serial number: `bash flash.sh 1050012345`.

Manual equivalent (what the script runs):

```bash
nrfutil device program --firmware merged_CPUNET.hex --core network \
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_NONE
nrfutil device program --firmware merged.hex --core application \
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_SYSTEM
```

## 5. Watch it boot

Open the DK's serial console at 115200 baud. The DK enumerates several serial
ports; the console is usually the first.

- **macOS:** `screen /dev/cu.usbmodem<something>1 115200` (tab-complete the
  name; `ls /dev/cu.usbmodem*` lists candidates)
- **Linux:** `screen /dev/ttyACM0 115200`
- **Windows:** PuTTY, connection type Serial, the first of the DK's COM ports,
  speed 115200

The boot log prints the Matter onboarding QR code URL and the manual pairing
code. Press the DK's RESET button to print them again. If the log scrolled past,
that is fine; both codes reprint on every boot.

## 6. Add it to Apple Home

1. Open the QR code URL from the boot log on any other screen (it renders the
   QR code in the browser), or keep the 11-digit manual pairing code handy.
2. In the Home app on the iPhone: add an accessory, then scan the QR code, or
   choose the manual option and type the pairing code.
3. Keep the phone next to the DK during commissioning (it starts over BLE
   before the lock joins Thread). Expect it to take a minute or two.
4. The firmware carries Matter test certificates, so Home warns about an
   uncertified accessory; choose to add anyway. That warning is expected for
   this evaluation build.
5. Once commissioned, Home provisions the key into Wallet on its own; there is
   nothing to tap or approve for this step. Then walk up with the phone
   (unlock on approach) or tap it on the NFC antenna.

## 7. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `nrfutil device list` shows nothing | Wrong USB connector (use the J-Link one, next to the power switch), power switch Off, or J-Link software not installed |
| Flash fails or verify errors | Reseat USB, power-cycle the DK, retry; make sure no other debugger session (RTT, IDE) holds the J-Link |
| No boot log on the serial port | Wrong port among the DK's several; try the next one, then press RESET |
| Boot log reports no UWB device ID, or approach unlock never triggers | The DWM3000EVB power-select jumper (see Assemble), or the shield is not fully seated |
| NFC tap does nothing | Re-check the 8-wire map against the table above, especially the A2 to A5 SPI lines |
| Home cannot find the accessory | Home hub missing or not a Thread border router, phone Bluetooth off, or the lock was already commissioned (reflash to wipe) |
| Commissioning fails near the end | Usually the Thread join: make sure the hub is online and in the same home, then remove the half-added accessory and retry |
| No key appears in Wallet | Give it a few minutes after commissioning; confirm the iPhone runs iOS 26 or later and has a UWB chip |

More depth, wiring checks, and a radio self-test:
<https://github.com/asxeem/openaliro/blob/main/docs/troubleshooting.md>

## Notes

- This is evaluation firmware with Matter test certificates. Do not put it in
  charge of a door you care about.
- Reflashing wipes the previous commissioning; remove the stale accessory from
  the Home app before re-adding.
