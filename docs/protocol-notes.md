# Aliro protocol notes

Working notes on the protocol behavior observed while building and debugging this
firmware. Written incrementally: this file currently covers **time synchronization
and time-based credential validity**. Discovery, auth, the M1-M4 ranging-setup
exchange, the STS derivation ladder, and channel hopping are not written up yet.

File references use paths inside the fetched workspace (`workspace/`); upstream
line numbers are as of the pins in `west.yml` / `bootstrap.sh`.

## Time synchronization and credential validity

### How the reader gets wall-clock time

- The wall clock on the Zephyr CHIP port is **RAM-only**: `SetClock_RealTime()`
  stores an offset against the monotonic clock (`gBootRealTime`,
  `modules/lib/matter/src/platform/Zephyr/SystemTimeSupport.cpp`). Any reset,
  warm or cold, erases it.
- The only things that set it are the Matter Time Synchronization cluster's
  `SetUTCTime` command and a configured trusted time source. **Apple Home sends
  `SetUTCTime` exactly once, at commissioning** (observed in a re-pair capture,
  2026-07-17). It configures no `TrustedTimeSource` and no `DefaultNTP`, so the
  clock is never set again. The device emits `TimeFailure` at every boot.
- Matter's **Last Known Good Time (LKGT)** is persisted in the fabric table, but
  it is only written at NOC install (certificate notBefore) and inside the
  cluster's `UpdateUTCTime`
  (`src/app/clusters/time-synchronization-server/time-synchronization-cluster.cpp:132`).
  Nothing refreshes it periodically, so it stays frozen at the commissioning
  timestamp, which lands roughly 30 s **before** the first Access Document is
  minted.
- The Nordic add-on's `GetCurrentTime()`
  (`subsys/aliro/time_utils/src/time_utils_chip.cpp`) returns the wall clock
  when valid and otherwise **silently substitutes LKGT**, converting "time
  unknown" into a confidently wrong answer. `GetCurrentUnixTime()` has no
  fallback and simply fails.

### Access Document validity

Access Documents are minted on demand with `validFrom` set to the phone's
current time and `validUntil` effectively unbounded (year 4001). The check in
`VerifyValidityPeriod`
(`applications/matter-aliro-door-lock-app/src/aliro/interface_impl/access_document.cpp`),
which runs after the document signature has been verified, therefore degenerates
to: **the reader clock must not be behind the phone clock**.

Failure chain after any reboot, without the fixes below:

1. RAM clock is gone; `GetCurrentTime()` falls back to LKGT.
2. LKGT predates every document ever minted.
3. Every fresh document is rejected as not-yet-valid
   (`Current time is outside the Access Document validity period`).
4. Step-up fails closed, permanently, until the lock is re-commissioned.

### Fixes carried by this repo (`integration/patches/`)

- **`aliro-doc-time-ratchet.patch`** (`CONFIG_DOOR_LOCK_TIME_CONCEPT_RATCHET`,
  default y): when a signature-verified document's `validFrom` lies ahead of
  the reader clock, ratchet the system clock forward to `validFrom` (never
  backward) instead of failing. Heals from issuer-signed material with no
  infrastructure. Trade-off: an issuer that signs a future-dated document can
  advance the clock early, weakening not-before enforcement.
- **`aliro-time-persist.patch`** (`CONFIG_DOOR_LOCK_TIME_CONCEPT_PERSIST`,
  default y): a 60 s poll persists the wall clock through the app's
  `KeyValueStorage` (settings key `dl/UT`) once per hour, immediately when the
  clock first becomes valid, and after any backward correction; at `AliroInit`
  the system clock is seeded from the stored value if (and only if) it is
  invalid. Restored time understates real time by at most one hour plus the
  power-off duration. It deliberately does **not** refresh Matter's fabric
  LKGT: LKGT feeds CASE certificate-validity checking, and a ratchet-poisoned
  time must not be persisted into that path.
- **Interplay**: persist alone fixes the common case (reboot after long
  uptime); the ratchet alone covers documents minted while the lock was
  powered off. Each covers exactly the other's blind spot. Because of persist,
  a ratchet advance now survives reboot; this is documented in the ratchet
  Kconfig help.

### Interaction: the BLE advertisement's Dynamic Tag (found on bench, 2026-07-17)

The Aliro BLE advertisement embeds an expiry timestamp of `lock unix time +
CONFIG_DOOR_LOCK_ALIRO_BLE_SERVICE_DYNAMIC_TAG_EXPIRY_DURATION_S` (default
900 s) when the wall clock is valid (`aliro_service.cpp`,
`PrepareAdvertisingDataLocked`), and "expiry unavailable" when it is not.
iPhones accept the unavailable form but silently ignore an advertisement whose
expiry lies in their past. A clock that is valid but *behind* real time by
more than the window therefore kills unlock-on-approach for every phone, while
NFC and Matter keep working. Both of this repo's time fixes create exactly
that state (the restored clock understates real time by the accumulated
power-off duration; a ratcheted clock understates it by the presented
document's age), so `integration/overlays/woz-aliro.conf` disables the Dynamic
Tag until a real time source exists. Stock firmware never hits this: its clock
is either fresh (before the first reboot) or invalid (after), never
valid-but-stale.

### Deferred: network time (SNTP / DefaultNTP)

Real network time is the only fix that makes expiry and schedule enforcement
accurate, but plain SNTP is unauthenticated: it would be the only input to the
trust chain controllable by an attacker on the home network or DNS path
(backward spoof: unlock denial; forward spoof: weakened not-before without
issuer keys). It also depends on the border router offering a routable path
(IPv6 NTP or NAT64/DNS64) and must be strictly fail-open for offline homes.
If built later: clamp to never-before-firmware-build-time, cap backward steps,
run async with backoff, and rank it below `SetUTCTime` as a source.

### Bench validation

1. Commission, wait ~1 min after the clock becomes valid (first persist write),
   reboot: boot log shows `Wall clock seeded from persisted value`, and the
   first step-up passes with no ratchet warning.
2. Power off for a while, mint a fresh document (unlock from a second
   same-account iPhone), power on: first step-up logs
   `system time ratcheted forward` and unlocks.

The temporary DBG overlay lines in `integration/overlays/woz-aliro.conf`
(`DOOR_LOCK_APP_LOG_LEVEL_DBG`, `DOOR_LOCK_ALIRO_TIME_UTILS_LOG_LEVEL_DBG`)
surface the decisive `Current time / validFrom / validUntil` values during
these tests and should be reverted afterwards.
