# Troubleshooting and FAQ

Common issues, grouped by where they show up. Deeper protocol background is in
[`protocol-research.md`](protocol-research.md) (on-air behavior) and
[`protocol-notes.md`](protocol-notes.md) (firmware time and credential behavior).

Everything below the ESP32 section is about the primary nRF5340 DK build. For the
ESP32-S3 ports, start with [ESP32-S3 ports](#esp32-s3-ports) and then the much longer
[gotchas log](esp32-gotchas.md), which records every trap hit during that
bring-up with its symptom and fix.

## Build and flash

**`make build` can't find the toolchain.** The NCS v3.3.0 toolchain is a one-time
per-machine install, separate from `make bootstrap`:
`nrfutil sdk-manager toolchain install --ncs-version v3.3.0`. All builds run through
`nrfutil sdk-manager toolchain launch … west`; a bare `west` is not used.

**A config change flashed but did not take effect.** A change to net-core configuration
needs a full erase: use `make flash-erase`, not `make flash`. `make flash` is app-core
only and leaves the net-core image in place.

**`make term` shows nothing.** The console and Zephyr shell are on the DK's VCOM1; VCOM0
is silent. `make term` auto-detects VCOM1; override with `PORT=` if detection picks the
wrong port.

**Build succeeds but the image does not fit.** The default configuration targets a full
flash budget (app FLASH is ~89.7%). Extra features may need a config trim; build with
`PRETTY=1` for readable size output.

## Unlock behavior

The first diagnostic question is always: **does tap still work?** Tap exercises the BLE
transport, provisioning, and credentials. If tap works and only approach fails, the fault
is UWB-specific, not in the credential path.

**Tap works, approach never ranges.** Either no common protocol version was negotiated, or
the reader never emitted the `0x98` "URSK ready" trigger, so the phone reports
`URSK_Unavailable`. See [`protocol-research.md`](protocol-research.md) §4 and the field
guide in §10.

**Approach worked, then stopped after a reboot.** This is the time/credential-validity
path, not UWB. The RAM wall clock is erased on reset and falls back to a stale Last Known
Good Time, so freshly minted Access Documents are rejected as not-yet-valid. See
[`protocol-notes.md`](protocol-notes.md); this repo carries the ratchet and persist fixes
that address it.

**Approach stopped but tap and Matter still work, no reboot involved.** If the clock is
valid but behind real time by more than the advertisement window (default 900 s), phones
silently ignore the BLE advertisement because its dynamic-tag expiry lies in their past.
This is the interaction documented in [`protocol-notes.md`](protocol-notes.md); the board
overlay disables the dynamic tag until a real time source exists.

**Ranging dies after walking out of range, or after about 12 hours.** Expected: the URSK
has a 12-hour TTL and is dropped when the BLE link drops. A fresh access transaction
(cheap, via the fast path) re-arms it. See [`protocol-research.md`](protocol-research.md)
§8.

**Setup (M1-M4) completes but there are zero distance reports.** A radio-path or
parameter problem, not a control-stack one: check the antenna and channel (5 or 9),
confirm a time sync happened (wrong listen window otherwise), and check for a negotiated
parameter mismatch, which yields a different SaltedHash and therefore a different STS with
no shared frames. See [`protocol-research.md`](protocol-research.md) §6-§7.

## Hardware

Pin assignments are defined in
[`ports/nrf5340dk/overlays/dw3000-nfc.overlay`](../ports/nrf5340dk/overlays/dw3000-nfc.overlay),
which is the source of truth. If the overlay changes, the wiring must change with it.

**The DW3000 is a 3.3 V part.** Power the DWM3000EVB from a 3.3 V rail, not 5 V. Share a
common ground with the host board.

**No SPI response / DW3000 not detected.** Confirm the EVB is powered (see above), the SPI
lines match the overlay, and the reset and IRQ lines are wired. `make selftest` builds a
boot self-test that exercises the radio bring-up with no phone present, which isolates a
wiring problem from a protocol one.

**The DWM3000EVB has its own power-select jumper.** Wiring the rails correctly is not
enough if that jumper selects the wrong source: SPI then fails silently, with no valid
device ID and a responder that never listens. Check it before suspecting software.

## ESP32-S3 ports

Full detail lives in [`docs/esp32-gotchas.md`](esp32-gotchas.md);
this is the short triage list.

**`dwt_probe failed: -1` the first time a phone reaches M4.** The DW3000 was never
brought up at boot, so the first SPI touch happens inside a NimBLE host callback, where
the shallow stack and missing init make probing fail. Bring the radio up once from a
dedicated startup task instead; both ports now do this.

**The bolt moves but the Wallet never animates.** Driving the lock is not the signal iOS
watches. The reader must send the Reader-Status-Changed grant message over the BleSK
channel; without it iOS shows only a plain Matter accessory notification. Neither the
phone's own computed distance nor the advertisement tag is the gate.

**The phone disconnects about 1.8 s after a successful EXCHANGE (reason 531).** The
reader did not send Reader-Status-Access-Protocol-Completed. It is mandatory, not
optional.

**`GeneralError URSK_Unavailable` at M1.** The ranging session id is derived from the
AUTH0 transaction id, not chosen by the reader. A hardcoded session id names a session
the phone has no key for. This is never a wrong-URSK-value problem: M1 carries no
URSK-derived material, so a value mismatch would surface later, at M2-M4 STS.

**`protocol 0 unsupported` fed to the ranging engine.** Ranging SDUs ride their own GCM
channel keyed from BleSK, with fresh per-direction counters, not the credential-auth
channel. Seeing the raw envelope type as a protocol number means the channel split was
missed.

**Ranging setup completes, POLL and Response look clean, but no distance is ever
computed.** On ESP32 this is usually a real-time fault, not a logic fault: a per-round
blocking log in a path with a 2 ms deadline starves the DW3000 ISR task, so the Final
callback dispatches too late to catch the phone's Final_Data. Throttle hot-path logging
first, then look at SPI transaction cost.

**Negative or absurd distances.** Suspect cross-round timestamp mixing before antenna
calibration. If the Final_Data of one round is decoded after the next round has
overwritten the shared timestamps, the arithmetic produces plausible-looking but wrong
values that still pass the integrity gate. Snapshot the intervals at Final capture. No
antenna calibration was needed on this hardware.

**Approach unlock works, then the bolt relocks 5 s later while the phone is still
there.** A fixed auto-relock timer fights approach unlock. Set `AutoRelockTime = 0` and
drive relock from proximity with hysteresis.

## Still stuck

Open an issue with the firmware commit, target, and console log; see the bug report
template. For anything security-sensitive, use private reporting instead (see
[`../SECURITY.md`](../SECURITY.md)).
