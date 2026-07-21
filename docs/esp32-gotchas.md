# ESP32-S3 Aliro port — gotchas and observations

Hard-won, non-obvious findings from porting the Aliro UWB door-lock reader to ESP32-S3
(ESP-IDF + esp-matter + NimBLE + DWM3000EVB). Each entry is a trap actually hit on the
bench: what it looks like, why it bites, and how to avoid or apply it.

Ports live under [`ports/esp32`](../ports/esp32) (reader / crypto / BLE components and
the standalone bring-up app) and [`ports/esp32/apps/matter-lock`](../ports/esp32/apps/matter-lock) (the Matter
door-lock app that hosts the reader). The ranging engine is `modules/woz_uwb`, shared
byte-for-byte with the nRF5340 build.

> Verification convention below: **VERIFIED** = confirmed on silicon or byte-exact
> against the reference binary/KAT; **BENCH-GATED** = built + host-tested but not yet
> confirmed against a live iPhone.

---

## 1. Toolchain, build & flash

### 1.1 `make flash` aborts with `awk: towc: multibyte conversion failure`
**VERIFIED.** The Makefile classifies USB serial ports by piping `ioreg -l -w0` through
`awk`. Some attached USB device ships a non-UTF-8 string descriptor; under a UTF-8
locale `awk` aborts mid-stream (`towc` conversion), which kills **both** port detection
and the SEGGER/J-Link safety guard — so `make flash` fails with "no ESP serial port
found" even though the board is plugged in.

- **Fix:** prefix the `awk` invocation with `LC_ALL=C` (byte-wise, locale-independent).
- **Subtlety:** `LC_ALL=C ioreg | awk …` does **not** work — the env prefix applies only
  to the first command in the pipeline. It must be `ioreg | LC_ALL=C awk …`.
- Applied at both `awk` sites in `ports/esp32/apps/matter-lock/Makefile` (the RESOLVE_PORT table
  build and the `ports` target).

### 1.2 Never flash the SEGGER/J-Link (nRF) port
**VERIFIED.** With both an nRF5340DK and the ESP32-S3 attached, port auto-detection can
see the J-Link (SEGGER, USB vendor `0x1366`). Flashing esptool to it is wrong and can
disrupt the nRF. The Makefile refuses that vendor and only auto-selects the ESP devkit's
bridges — WCH CH343/CH9102 (`0x1A86`) or Espressif native USB (`0x303A`), both
`/dev/cu.usbmodem…`. `ioreg` reports vendor ids in decimal, which is why the Makefile
compares against `4966` / `6790` / `12346`. Keep the guard; don't bypass it.

### 1.3 `make flash` port-holder guard
`make flash` refuses if another process holds the port (a stuck `monitor`), with a clear
message. Override with `FORCE=1` only after confirming no other session owns the port.
(commit `8ce132a`)

### 1.4 Host tests are the target's proof
The crypto core (`aliro_hash.c`) compiles **host == target** so host KATs pin target
behaviour. Run `ports/esp32/test/run.sh` before believing any crypto change. A
compact AES-256-GCM host double (`aliro_prim_host.c`) lets the KATs run without PSA.
Build success is not proof — the wire/crypto bugs below all built cleanly. The shared
`modules/woz_uwb` logic has a second host harness, `tests/host/run.sh` (558 tests),
which compiles the shim **without** `ESP_PLATFORM` — that is the proof an ESP-only guard
didn't regress the nRF path.

---

## 2. Hardware bring-up

### 2.1 DWM3000 EVB power jumper (the multi-day one)
**VERIFIED.** DW3000 SPI comms silently failed until the EVB power jumper was set
correctly. Symptom before: no valid device ID / responder never listens. After: `start_
aliro()=0`, responder listening, a 36-byte RX frame arrives. If DW3000 bring-up looks
dead, check the **board power-select jumper first** — it is not a software problem.

### 2.2 Probe the DW3000 at boot, never from the BLE-host callback at M4
**VERIFIED (regression) → BENCH-GATED (fix).** The standalone reader app (`ports/esp32/apps/reader`) probes the
DW3000 at boot (`app_responder_start()` in `main.c`). The `matter-lock` app dropped
that, so the first DW3000 touch happened at M4 — inside the NimBLE host callback
(`aliro_ranging_feed → engine → woz_uwb_start_aliro → dwt_probe`). There `dwt_probe`
returns `-1` (radio init `-5`): no prior bring-up + a shallow callback stack. Symptoms:
`dwt_probe failed: -1`, then on reconnect `spi_bus_initialize: SPI bus already
initialized`.

- **Fix:** bring the radio up once in the clean reader-startup task. `aliro_ranging_init`
  does a throwaway `woz_uwb_start_aliro` (zero URSK) + `woz_uwb_stop`; `uwb_min`'s
  `g_radio_ready` latches, so M4's `ccc_prepoll_listen` skips the probe and only
  re-applies the negotiated channel.
- **Corollary bug:** `dw3000_spi_init` re-added SPI devices on a second call (leaking the
  old handles) — made idempotent (`return 0` if already up). The boot probe means M4
  never re-enters `dw3000_hw_init` anyway, so the "already initialized" path is avoided.
- **Rule:** heavy SDK bring-up (`dwt_probe` / `dwt_initialise`, OTP reads) belongs in a
  dedicated startup task with a real stack, not a protocol-event callback.

### 2.3 Onboard WS2812 status LED is on GPIO48
**VERIFIED.** The ESP32-S3 dev board's addressable status LED is GPIO48 (single WS2812),
used for bolt-state indication. (commits `ad9a63b`, `ed38895`)

---

## 3. BLE / NimBLE coexistence with Matter

### 3.1 The reader must "attach" to Matter's NimBLE host, not spin up its own
**VERIFIED.** esp-matter already owns the NimBLE host for commissioning. A second BLE
stack instance crashes / reinit-loops. The Aliro reader runs in **attach mode**: it
coexists on Matter's host rather than initialising its own. (commits `67234fa`,
`996a8d5` — the latter fixed a reader-reinit crash loop by keeping BLE up.)

### 3.2 Keep BLE advertising up across reader restarts
Tearing BLE down/up around reader lifecycle caused crashes. The reader keeps the GAP
advertising alive and re-arms the session instead of cycling the stack. (commit
`996a8d5`)

### 3.3 The Aliro transaction must stay on the nimble_host task
**VERIFIED.** Driving the Aliro APDU exchange from another task races the host. The whole
credential-auth + ranging lifecycle (create / feed / teardown + the engine's transmit &
event callbacks) runs **synchronously on the BLE-host task**, so no locking is needed
there. Provisioning state shared with the REPL task (`s_trust`, `s_last_cred`) is the
one thing guarded by a FreeRTOS mutex. (commit `04bd8cc`)

### 3.4 Advertising service data
The reader advertises the Aliro 0xFFF2 service with a dynamic tag for phone
approach-connect. Note the separate `DYNAMIC_TAG` staleness trap: approach-unlock dies
when the clock is valid but far behind real time (advertisement expiry tag), fixed on
the nRF side by disabling the dynamic tag — watch for the analogue here. (commit
`5a4e6c4`)

---

## 4. Aliro credential-auth crypto & key schedule

These were resolved by decompiling `libaliro_ble.a` (Cortex-M33) and cross-checking a
readable reference `crypto.cpp`, then pinning with host KATs. **Clean-room: facts only,
no decompiled source enters the repo.**

### 4.1 GCM per-direction counters start at **1**, not 0
**VERIFIED (byte-exact KAT + silicon).** Aliro §8.3.1 starts both per-direction secure-
channel counters at 1 (HomeKey starts at 0 — do not copy HomeKey here). The first
inbound decrypt (AUTH1Response) uses device_counter = 1. Starting at 0 fails the GCM tag
cleanly. `aliro_secchan_init` sets both counters to 1.

### 4.2 GCM nonce layout
`nonce = 8-byte big-endian direction (0 = reader-seal, 1 = device-open) || 4-byte
big-endian per-direction counter`. Separate, non-wrapping counters per direction.

### 4.3 The salt's two "unresolved" sub-fields (§8.3.1.13)
**VERIFIED.** The AUTH1 tag only decrypts once the session salt is exact:
- salt field 1 = **`reader_group_identifier_key.x` = pub(signingKey).x** — this is the
  reader's *signing* public key X, **not** the device ephemeral key (that was the bug).
  Derived via `aliro_ec_p256_pub_from_priv`; refreshed on identity load and on both
  Matter provisioning paths.
- salt item 4 = **interface byte** (`0xC3` BLE / `0x5E` NFC).
- salt item 10 = the phone's **`0xA5` SELECT-response proprietary-info TLV**, captured
  from its op-0x05 message; CSA v1.0 default `a5 08 80 02 0000 5c 02 0100`.

### 4.4 Key-block layout (one HKDF expand → 160 bytes)
`Z = SHA-256(ecdh_shared(32) || 0x00000001 || txid(16))`; then
`block160 = HKDF-SHA256(salt=CreateSalt transcript, IKM=Z, info=devicePubX(32), L=160)`.
Offsets in the block: `ExpeditedSKReader@0`, `ExpeditedSKDevice@32`, `StepUpSK@64`,
**`BleSK@96`**, **`URSK@128`**. Because it is one expand, **if auth works (ExpeditedSK@0
is right) the URSK@128 is byte-identical on both sides** — a fact that ruled out a lot of
false leads during ranging debug.

### 4.5 HKDF salt-vs-info binding is not in the blob
The one derivation detail the decompiled blob did not settle (which argument is salt vs
info) came from **readable** reference source: `DeriveKey` = PSA HKDF with
`INPUT_SALT=salt`, `INPUT_SECRET=Z`, `INPUT_INFO=info`. When the binary is ambiguous,
look for a readable reference before guessing.

### 4.6 APDU / wire codec specifics
ISO7816 case-4 short form wraps AUTH0/AUTH1/EXCHANGE. Opcodes: **AUTH0=0x80,
AUTH1=0x81, EXCHANGE=0xC9**. The ECDSA transcript uses usage separators
`kReaderUsage=415d9569` / `kUserDeviceUsage=4e887b4c`. The L2CAP envelope is the 4-byte
header `[type & 0x3F][opcode][len_be16]`. EXCHANGE payload for the ranging flow is bare
`98 00` (URSK-ready, empty TLV) — **VERIFIED** the reference passes an empty
`optional<ReaderStatus>` here, so no `0x97` status tag.

### 4.7 The credential-auth §14 KAT is member-confidential
A host KAT reproduces the Aliro 1.0 §14 worked example byte-exact (salt, Kdh,
ExpeditedSKReader, BleSK, URSK, the split). It lives in **scratchpad only and is never
committed** — the §14 vectors are CSA member-Confidential. Same for the BleSK message KAT.

---

## 5. Ranging transport (post-auth M1–M4) — the deep ones

The reference reader relied on the **closed `libaliro` stack** to seal/open ranging SDUs
*below* the open glue. Phase 3 replaced that stack, so every one of these had to be
reimplemented from scratch — and each was a separate live-iPhone failure.

### 5.1 Ranging SDUs ride a SEPARATE GCM channel from the AP channel
**VERIFIED (spec §11.8 + silicon).** Proto-1/2/3 ranging SDUs do **not** use the
credential-auth (AP) secure channel. They use their own AES-256-GCM channel:
- Keys `BleSKReader` / `BleSKDevice` = `HKDF-SHA256(ikm = BleSK@96, info =
  "BleSKReader"/"BleSKDevice", salt = reader_supported_versions ||
  user_device_selected_version)` = `01000100` for v1.0-only.
- **Fresh** per-direction counters starting at **1** (not continued from auth).
- Feeding a proto-0 EXCHANGE response raw to the ranging engine yields `protocol 0
  unsupported` (byte 0 = the 0x00 envelope type) — a classic symptom of missing the
  channel split.

### 5.2 Ranging SDU framing — AAD carries the PLAINTEXT length
**VERIFIED (host KAT 15/15).** Wire form:
`[proto][id][(len_plain + 16)_be16][ciphertext || 16-byte tag]`, and the GCM **AAD is the
4-byte header with the *plaintext* length** `[proto][id][len_plain_be16]`, **not** the
wire length. Getting the AAD length field wrong is the single highest-risk mistake here;
it fails the tag with no other signal. Implemented in `aliro_msg_seal` / `aliro_msg_open`.

### 5.3 The missing step: Reader-Status-Access-Protocol-Completed
**VERIFIED against the reference binary.** After EXCHANGE success the reader **must** send
Reader-Status-AP-Completed (proto-2 id-3, BleSK-sealed) or the device stalls ~1.8 s and
disconnects (reason 531). Payload = `02 03 00 04 00 02 20 00` (TLV: attr-id 0, len 2,
value `[cap = 0x20][ReaderState = 0x00 Secured]`). Disassembly of
`CompleteAccessProtocolState::Run → BleTpCompleted` confirmed these exact bytes,
including ReaderState = 0x00.

### 5.4 `00 02 00 00` is EXCHANGE SUCCESS, not a ranging cue
The `00 02 00 00` body that comes back on the EXCHANGE response is §8.3.3.5.5 success
(len `0x0002`, error `0x0000`). It is not a signal to start ranging; don't hand it to the
engine.

### 5.5 THE session-id trap: it is derived from the AUTH0 TXID, not chosen
**VERIFIED by disassembly.** The ranging session-id is **not** the reader's free choice.
The device computes it from the 16-byte AUTH0 transaction id it received:

```
GenerateCryptoMaterials()  →  fills AccessProtocolCrypto[0..16] with a random TXID
GetSessionId()             =  rev(this[0xc])  =  big-endian(txid[12..15])
```

The device indexes its URSK by that session-id. Advertise a hardcoded session-id in M1
and the device replies with **GeneralError URSK_Unavailable (code 3)** at M1 and
disconnects — it has no URSK for the session you named, even though the URSK *value*
matches. Fix: M1's session-id must be `(txid[12]<<24)|(txid[13]<<16)|(txid[14]<<8)|
txid[15]`. This was the final blocker after auth + transport were correct.

### 5.6 M1 carries no URSK crypto — so `URSK_Unavailable` ≠ wrong URSK value
M1 (proto-1 id-0) contains only config attributes (config-id, pulse-shape, channel,
session-id). It carries no URSK-derived material, so a `URSK_Unavailable` at M1 means the
device has **no URSK installed for that session** (see 5.5), never a URSK value mismatch —
a value mismatch would surface later at M2/M3/M4 STS. This distinction saved a lot of
misdirected debugging.

### 5.7 Device-initiated M1
The engine emits M1 when the phone sends its Initiate-Ranging-Session (proto-2 id-1,
attr INIT_RANGING = 0x00), matching the observed iPhone. Do **not** eagerly send M1 on
StartRangingSession. (`aliro_uwb_msg.c:1164`)

### 5.8 Counter chain across the ranging handshake
Reader-direction (enc): AP-Completed = 1, M1 = 2, M3 = 3.
Device-direction (dec): the phone's first BleSK message (a proto-3 config) = 1,
Initiate-Ranging = 2, its event/M2 = 3 … Each side increments per message it *sends* on
the BleSK channel, including the proto-3 config the phone leads with.

### 5.9 proto-3 SupplementaryService is ignorable
The phone leads ranging with a proto-3 SupplementaryService config blob. **VERIFIED** the
reference's ranging engine (same Qorvo code) also returns "protocol 3 unsupported" for
it — both reference and port ignore it. It is not a step we skip; don't chase it.

### 5.10 THE Wallet animation gate: send Reader-Status-Changed on grant (step 23)
**VERIFIED byte-exact (disassembly + live iPhone).** Driving the bolt is not enough: iOS
plays the Wallet unlock animation only when the reader tells the phone it granted access
over the BleSK channel — the "Reader Status Changed" message, Aliro transaction
step 23 (the grant-phase sibling of 5.3's AP-Completed). Without it the port unlocked the
bolt locally, Matter saw the state change and posted a Matter *accessory* notification, but
the Wallet never animated. The phone's own computed distance is **not** the gate.

Payload (proto-2 id-2, BleSK-sealed) = `02 02 00 04 00 02 04 01` — one State Attribute
(attr-id 0, len 2) = `[OperationSource = 0x04 (this device, BLE+UWB Aliro flow),
ReaderState = 0x01 Unsecured]`; relock is identical with ReaderState = 0x00 Secured.
Disassembly of `AliroStack::SendReaderStatusChangedMessage → BleTpReaderStatusChanged`
confirmed these exact bytes. The 65-byte access-credential public key is **not** serialized
— the library takes it only to select which connection to notify, then drops it.

Send Unsecured on grant, Secured on relock, from the proximity relock task; post it onto
the BLE-host task so it serializes with the other BleSK seals (counter stays monotonic).
(commit `ba612c8`)

---

## 6. UWB DS-TWR ranging engine — the real-time layer

Once M1–M4 negotiate the session, the actual double-sided two-way ranging runs on the
DW3000: the phone drives Pre-POLL → POLL → (our) Response → Final → Final_Data every
~192 ms block, and the responder must arm each frame inside a ~2 ms slot. **This layer
is shared with the nRF5340 port (`modules/woz_uwb`), where it already worked — so nothing
here is a logic bug. Every trap below is the ESP32 being a slower, jitterier real-time
target than the nRF, and the fixes claw back the microseconds nRF gets for free.** All
**VERIFIED on silicon**: continuous positive DIST every round and the phone unlocks the
door.

### 6.1 The arm-latency-vs-2ms-slot budget is the whole game
Reactive arming fights a fixed deadline: `CCC_RX_SLOT_HI32 − CCC_RX_POLL_LEAD` (= 459000
hi32 units = **1836 µs**; the hi32/`dsys` unit is 4 ns, so `/250 = µs`). If the
Pre-POLL→arm path exceeds it, the delayed RX never opens and the POLL is lost
(`ARM FAIL … LATE`). On nRF this is trivial; on ESP every µs of SPI and dispatch counts,
which is why 6.2–6.7 exist.

### 6.2 DW3000 SPI: disable DMA (the biggest single win)
**VERIFIED.** A boot micro-benchmark (32 back-to-back DEV_ID reads) measured
**~84 µs/transaction** with `SPI_DMA_CH_AUTO`; holding the bus lock only saved ~15 µs, so
the rest is the per-transaction DMA-descriptor + `esp_cache_msync` path. For the small
STS/register writes on the arm critical path that setup dwarfs the bit-time. Fix: init the
bus **`SPI_DMA_DISABLED`** so ≤64-byte transfers use the CPU data registers directly, and
chunk anything larger into ≤64-byte bursts under one CS-low window (the DW3000 streams
sequentially). Only the DW3000 is on SPI2 (the status LED is on RMT), so this is safe.
This is what carried the chain to a sustained lock. (commit `d9051c8`)
- Clock is **not** the lever: 2 MHz vs 8 MHz was ~75 vs 84 µs — the ~6 µs of bit-time is
  lost in ~70 µs of fixed overhead.

### 6.3 An SP3-ND Final completes with RXFR|CIADONE, not RXFCG
**VERIFIED.** The POLL is an SP3 STS frame carrying FCS (completes `st=0x6700`, has
RXFCG=0x4000). The **Final is SP3-ND (STS only, no PHR/payload)** — it completes
`st=0xa7b0` with RXFR|CIADONE and **no RXFCG**. Any Final-detect that waits on RXFCG hangs
forever; watch CIADONE(0x400). A synchronous busy-wait for the Final *inside* the POLL
callback also fails: the SP3-ND completion lands after the handler returns, and the spin
wedged the receiver / tripped the watchdog. Capture the Final in its own async callback.

### 6.4 "Negative distance" was cross-round mixing, NOT antenna delay
**VERIFIED — a multi-run red herring.** t2/t3/t6 (POLL-RX / Response-TX / Final-RX) are
single globals. On ESP the phone's SP0 Final_Data lands only after the *next* round has
overwritten t2/t3, so recomputing the DS-TWR intervals in `final_data_decode` mixed this
round's t6 with the next round's t3 → km-scale garbage, or a **plausible-looking but wrong
negative distance that still passed the STS gate**. The `-783 mm` / `-333 mm` readings
looked exactly like an uncalibrated antenna delay and sent us chasing calibration. Fix:
snapshot `reply1 = t3−t2`, `round2 = t6−t3` at Final capture (same round), consume once
(`g_final_round_valid`). With correct pairing the distances are **positive and realistic
with no antenna calibration at all** (~0 mm at contact, ~21 cm for a phone at ~21 cm).
ESP-guarded so the nRF path keeps its original recompute-from-live-globals (nRF processes
Final_Data before the overwrite). (commit `d9051c8`) **Lesson: a constant-looking offset
that passes your integrity gate can be a data-pairing bug, not a physical calibration.**

### 6.5 THE Final_Data trap: blocking `printk` in the hot path starves the ISR task
**VERIFIED — the last blocker before the door opened.** `DIAGK` → `printk`, which is
**blocking** on ESP. `POLL result` + `FINAL result` printed **every round** (~185 chars);
at 115200 that cannot drain inside the ~6 ms burst of per-round callbacks, so the DW3000
ISR task (which runs `dwt_isr` + the callbacks) backs up. One cause, two symptoms:
- The **Final callback dispatches late** → the SP0 revert re-opens RX *after* the phone's
  Final_Data slot (~1 slot ≈ 2 ms behind the Final) → Final_Data never caught → **no
  distance ever computed**, even though POLL/Response/Final were flawless.
- The sustained backlog **trips the task watchdog** → spontaneous reset mid-session.

Fix: throttle `POLL result` and `FINAL result` to the first `CCC_RX_PREPOLL_LOG` (16)
rounds so the steady state is print-free (PREPOLL and RESP-txdone were already gated).
Moving a print *within* a callback cannot help — the callback itself was firing late.
**A per-round blocking log in a 2-ms-deadline path is a real-time bug, not just noise.**
(commit `677cfd1`)

### 6.6 Time-critical FIRST: revert to SP0 before the (throttled) print
The Final handler reverts SP3→SP0 (`revert_to_sp0_listen`) to catch the Final_Data; do it
**before** the diagnostic print, mirroring the POLL handler which arms the Response before
its stsq read + printk. (Redundant once 6.5 throttles the print, but the correct ordering
and cheap insurance.) (commit `677cfd1`)

### 6.7 STS key cache — the dURSK is per ranging CYCLE
**VERIFIED.** The STS key (dURSK) is constant across a whole ranging cycle (POLL,
Response, Final, and every block in the cycle share it), but each arm re-wrote all four
STS_KEY registers (~258 µs, ~40 % of the arm). Cache the loaded dURSK and skip
`dwt_configurestskey` when unchanged; the registers persist across the per-frame
IV/loadiv/mode reprogramming. Helps the Response/Final arms (same-cycle key = cache hit);
the POLL arm's key changes per block so it misses. ESP-guarded. (commit `d9051c8`)

### 6.8 Auto-relock: a fixed timer fights approach-unlock — drive relock from proximity
`create_auto_relock_time(door_lock_cluster, 5)` made the bolt relock 5 s after an unlock,
so a successful approach-unlock re-locked while the phone was still right there. A fixed
timer is fundamentally wrong for approach-unlock: you cannot both "relock after N s" and
"stay unlocked while the peer is present." Fix: set **`AutoRelockTime = 0`** — CHIP's
`DoorLockServer` skips scheduling when it is 0 (`VerifyOrReturnError(0 != autoRelockTime,
true)`) — and drive relock from proximity in `aliro_reader_task`: unlock at
`<= ALIRO_UNLOCK_RANGE_CM` (100 cm), hold while present, relock when the peer moves past
`ALIRO_RELOCK_RANGE_CM` (150 cm — hysteresis stops boundary flapping) or the ranging
session drops. Re-unlock within one session now works too (the old code debounced until
disconnect). Not a ranging fault; a lock-policy design choice.

### 6.9 ESP vs nRF: the logic is shared and proven; ESP is the real-time port
The whole Aliro/CCC/DS-TWR stack in `modules/woz_uwb` is compiled by **both** the nRF5340
Aliro app and this ESP port. When ESP misbehaved, the bug was never in that shared logic
(the nRF proves it) — it was ESP's slower SPI + jitterier callback dispatch. Two
consequences worth remembering: (a) lean on the nRF reference to confirm intent before
re-deriving on hardware; (b) any ESP-only tweak to the shared file must be
`#if defined(ESP_PLATFORM)`-guarded so the nRF build keeps the original path — the
snapshot (6.4) and STS-key cache (6.7) are both guarded, verified by the host suite
(`tests/host/run.sh`, 558/558) compiling the file *without* `ESP_PLATFORM`. (commit
`d9051c8`)

---

## 7. Reader state-machine & diagnostics

### 7.1 The GeneralError short-circuit must not fire during ranging
**VERIFIED.** The reader had a blanket rule: a proto-2 / id-0 Notification-Event mid-auth
is a device GeneralError, so read `payload[2]` as the code. During `PH_ESTABLISHED` those
events are **BleSK-encrypted**, so `payload[2]` is a ciphertext byte — the reader was
reporting `GeneralError 0x28` one run and `0xf6` the next (random ciphertext) and killing
the session before decrypting the real event. Guard the short-circuit to
pre-ranging phases; in `PH_ESTABLISHED` always BleSK-open + dump + feed the engine. This
is what finally surfaced the real `general error 3`. (`aliro_reader.c` ~line 583)

### 7.2 Read the *decrypted* error, and log it
General-error codes: `0 UNKNOWN, 1 RESOURCE_UNAVAILABLE, 2 WRONG_PARAMS, 3
URSK_UNAVAILABLE`. `WRONG_PARAMS (2)` points at M1 attribute values; `URSK_UNAVAILABLE
(3)` points at the session-id / URSK-install path (§5). The codes are only meaningful
once the SDU is opened.

---

## 8. Provisioning & identity

### 8.1 A per-boot random reader key changes the reader's identity every reboot
**VERIFIED.** The reader originally generated a random dev P-256 key at boot, so its
reader-id (and therefore the salt's reader_group_key) changed each power cycle — the
phone's stored credential no longer matched. Fixed by a **fixed, non-secret dev
identity** loaded from NVS (`aliro_prov` namespace, key `blob`): reader-id = signing
pub.X. (commit `ca937e2`)

### 8.2 DEV identity is dev-open + loud, not a bypass
With a DEV identity and no trust anchors, the reader accepts the presented credential
**and logs a warning**; it is an interim seam for real issuer-chain validation, not a
silent allow. Bench commands: `aliro-prov` (show id / trust / last cred), `aliro-trust`
(persist the last-presented credential key). Phase-4 Matter `SetAliroReaderConfig` writes
the same blob.

### 8.3 Zephyr-reserved NVS id wiped external NVS (cross-port cousin)
On the nRF side, a rollback unique-id mirror at Zephyr-reserved NVS id `0xFFFF` wiped
external NVS every boot (fixed `0xFFFF → 0xFFFE`). If ESP32 external-NVS credentials ever
vanish across reboot, suspect an id collision with a reserved slot.

---

## 9. Reverse-engineering methodology (for the next unknown)

- **The reference binaries are ground truth.** `workspace/ncs-door-lock-and-access-
  control/lib/aliro/bin/debug/cortex-m33/libaliro{,_ble}.a` are **debug** builds with
  symbols. Prefer the debug archive.
- **macOS `ar` cannot read GNU long-name (`//`) members.** Use a small Python GNU-ar
  parser (`elftools` + a manual `//` long-name table) to extract objects; the BSD `ar x`
  silently yields empty files.
- **Disassemble Thumb with capstone**, masking the low bit of `st_value` (Thumb symbols
  are odd-valued) before computing the section offset, or you disassemble one byte off
  and get garbage.
- **Log strings pin intent fast.** `.rodata` format strings (e.g. "BLE completed, state:
  %s, capabilities: %s") revealed the AP-Completed field layout before disassembling the
  builder.
- **Verify, don't assume.** The winning move for the session-id bug was disassembling
  `GetSessionId` / `GenerateCryptoMaterials` directly rather than trusting the summary.
  Every reader→phone message was checked byte-for-byte against the reference; only one
  (the session-id) actually differed.

---

## 10. Current status — approach-unlock works end to end

- **VERIFIED on a live iPhone (2026-07-20):** the full Aliro approach-unlock — the Wallet
  unlock animation plays as you walk up, then relocks on departure. The final gate was the
  reader→phone Reader-Status-Changed grant message (5.10); before it the bolt moved and
  Matter reported it, but the Wallet stayed silent.
- **VERIFIED on silicon (full path):** credential-auth (discovery / L2CAP CoC / AUTH0 /
  AUTH1 decrypt / device-signature / credential-trust / URSK) → M1–M4 ranging setup →
  **live DS-TWR**: continuous positive DIST every round, tracking the phone (d ~6–45 cm at
  arm's length, growing as it withdraws), then `Aliro trusted range NN cm (<= 100):
  unlocking` and auto-relock. No watchdog resets. Against a live iPhone.
- **No antenna calibration was needed** — see 6.4; the earlier "negative distance" was a
  cross-round pairing bug, not a physical offset.
- **Open polish (non-blocking):** per-round DIST jitter (the AccessManager already
  medians/filters it enough to unlock cleanly); auto-relock time (6.8); the phone's
  `general error 0` at end-of-session is post-unlock and benign.

### Commit map (newest first)
| Commit | What |
|---|---|
| `cf0c73d` | document the Reader-Status-Changed grant (message 23) gotcha |
| `ba612c8` | tell the phone the grant so the Wallet unlock animation plays (§5.10) |
| `7b892b6` | land the DS-TWR RESPONSE on the phone's negotiated slot |
| `6ba874b` | capture DS-TWR ranging bring-up gotchas (unlock working) |
| `677cfd1` | throttle hot-path ranging logs so DS-TWR closes and unlocks (§6.5, §6.6) |
| `d9051c8` | DMA-disable SPI, fence ESP-only ranging tweaks from nRF (§6.2, §6.4, §6.7, §6.9) |
| `1ef8946` | real-time DS-TWR responder — POLL→Response→Final→distance on silicon |
| `a517d71` | DW3000 radio bring-up (boot probe + wakeup-settle) + tooling (§2.2) |
| `84c2875` | Aliro ranging session-id + BleSK channel — M1-M4 green on silicon (§5.1, §5.5) |
| `f628beb` | ranging-setup diagnostic (hold M1, decrypt post-EXCHANGE SDUs) |
| `5f91235` | AUTH1 secure-channel decrypt — credential-auth green on silicon |
| `cef8255` | correct AUTH0 phase encoding — full ECDH auth accepted |
| `04bd8cc` | keep the Aliro transaction on the nimble_host stack |
| `f937f01` | wrap Aliro auth commands in ISO7816 APDUs (AUTH0 acceptance) |
| `69d24bb` | Aliro reader BLE discovery + L2CAP CoC for approach-unlock |
| `ed38895` | host test for the bolt-state LED policy |
| `5a4e6c4` | Aliro 0xFFF2 advertisement + dynamic tag (phone approach-connect) |
| `ad9a63b` | bolt-state LED on the onboard WS2812 (GPIO48) |
| `67234fa` | attach-mode Aliro reader — coexist on Matter's NimBLE host |
| `2aea20f` | make front door for the esp32-matter port (isolated) |
| `996a8d5` | interactive console + keep-BLE-up (fixes reader-reinit crash loop) |
| `af7a3a3` | print commissioning onboarding codes at boot |
| `d7131d3` | Phase 4.3 merge reader+UWB into Matter app (provision→handoff→range) |
| `14446c8` | Phase 4.2 Aliro reader delegate (BLE+UWB Home Key provisioning) |
| `9eba9c9` | Phase 4.1 esp-matter door-lock baseline (commissions to Apple Home) |
| `19b6239` | Phase 3.5 M1–M4 ranging-setup wiring (negotiated params) |
| `ca937e2` | Phase 3.4 reader provisioning seam (identity + credential trust) |

Earlier Phase-3.1/3.2/3.3 crypto and auth work (key schedule, wire codec, URSK handoff)
landed before this range; its gotchas are captured in §4. Everything above is merged to
`main`.
