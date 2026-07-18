# Troubleshooting and FAQ

Common issues, grouped by where they show up. Deeper protocol background is in
[`protocol-research.md`](protocol-research.md) (on-air behavior) and
[`protocol-notes.md`](protocol-notes.md) (firmware time and credential behavior).

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
[`integration/overlays/dw3000-nfc.overlay`](../integration/overlays/dw3000-nfc.overlay),
which is the source of truth. If the overlay changes, the wiring must change with it.

**The DW3000 is a 3.3 V part.** Power the DWM3000EVB from a 3.3 V rail, not 5 V. Share a
common ground with the host board.

**No SPI response / DW3000 not detected.** Confirm the EVB is powered (see above), the SPI
lines match the overlay, and the reset and IRQ lines are wired. `make selftest` builds a
boot self-test that exercises the radio bring-up with no phone present, which isolates a
wiring problem from a protocol one.

## Still stuck

Open an issue with the firmware commit, target, and console log; see the bug report
template. For anything security-sensitive, use private reporting instead (see
[`../SECURITY.md`](../SECURITY.md)).
