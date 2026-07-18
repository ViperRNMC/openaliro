# Protocol research: BLE + UWB proximity unlock

## Abstract

This report documents the on-air behavior of a phone-driven ultra-wideband (UWB)
proximity unlock: how a phone opens a fixed reader on approach. The phone conducts the
protocol exchange over Bluetooth LE, then the two run a UWB secure ranging exchange that
lets the reader measure distance before it unlocks. The scope here is deliberately
narrow: what reaches the air, and what must hold before ranging can begin.

The work began from a specific failure: NFC tap-to-unlock worked, but the door would not
open on approach. Tap working is a useful signal: it means the BLE transport,
provisioning, and credentials are healthy, so the fault is UWB-specific. The fallback to
NFC when UWB is disturbed is by design, which is why tap continued to work throughout the
period when ranging was dead.

> **Educational, research, and study use only.** These notes exist for learning,
> independent research, and personal study of how BLE + UWB secure ranging systems behave
> on the air. They are not an invitation to bypass, defeat, or gain unauthorized access
> to any access control system. Only work with hardware you own or have explicit, written
> authorization to test.
>
> This report is not affiliated with, endorsed by, or speaking for any company, standards
> body, or specification owner mentioned or alluded to. All protocol specifications,
> standards, trademarks, product names, and other intellectual property referenced or
> implied remain the property of their respective owners, along with every right,
> license, and disclaimer attached to them.

## 1. System under study

- The **phone** is the UWB initiator (the side sending poll packets). It carries a U-class
  UWB chip and runs iOS 26 or later.
- The **reader** is the fixed lock, acting as the UWB responder with a single anchor. The
  radio studied here is a bare DW3000-family part (a DW3110) on an nRF5340, with no
  dedicated UWB coprocessor, so the MAC, PHY, and STS are all implemented in host
  firmware.
- All protocol-level traffic rides on BLE. UWB carries no application data; it is purely
  the distance measurement. The unlock policy is entirely the reader's decision: in the
  configuration studied, it unlocks at 150 cm or closer and relocks past 180 cm, giving
  30 cm of hysteresis.

## 2. The full transaction, top to bottom

The complete exchange in order. Everything above the line is in the clear or
authentication-protected; everything below is encrypted under BleSK.

```
 1     Initiate Access Protocol                          phone → reader (clear)
 2-9   Access/authentication exchange (AUTH0 [± cert, AUTH1])
10-11  EXCHANGE (carries the zero-length "URSK-ready" trigger, tag 0x98)
------ everything below encrypted under BleSK ---------------------------------
12     Reader: "Access Protocol Completed"               reader → phone
13     Time Sync                                         phone → reader   (section 5)
14     (optional) Initiate Ranging Session               phone → reader
15-18  Ranging Setup M1 / M2 / M3 / M4                   (section 6)
       ... secure UWB ranging; distance computed at the reader ...   (section 7)
19-22  Suspend / Resume (either side)                    (section 8)
23     Reader Status Changed → unlock/lock decision      reader → phone
```

Once "Access Protocol Completed" passes, both sides drop the authentication session keys.
BleSK persists, and it guards all ranging control traffic from that point on (time sync,
M1-M4, suspend/resume).

## 3. Discovery and connection (all sniffable)

The reader beacons `ADV_IND` on LE 1M (and optionally coded S=2). The payload is service
data under 16-bit UUID `0xFFF2`. The fields relevant to reverse engineering:

- Byte 7, bit 7 is the "BLE + UWB flow supported" flag. This is the one bit a UWB-capable
  build sets and a control-only build leaves at 0; if it is 0, the phone will not attempt
  to range. For reference, the rest of that byte: bit 6 is the BLE-only flow, bits 4:3 are
  notification/error, bits 2:0 are the advertisement version.
- Byte 8 is TX power. Bytes 9-18 are a truncated reader group identifier plus a
  sub-identifier.
- Bytes 19-22 are the dynamic tag expiry as Unix time, or `0xFFFFFFFF` if the reader has
  no clock. Byte 23 is reserved.
- Bytes 24-30 are the dynamic tag itself: the first 7 octets of
  `AES-128(GroupResolvingKey, pad ‖ AdvA ‖ expiry)`.

The phone scans passively and identifies its own readers by recomputing the dynamic tag
with each group resolving key it holds. On a match, it sends `CONNECT_IND`. Conversely, a
reader that has gone stale or been reprovisioned is effectively invisible to the phone
even while advertising, because the tag no longer resolves.

The GATT and L2CAP handshake proceeds as follows. The phone reads a characteristic that
returns the dynamic SPSM (in `0x0080-0x00FF`), the supported protocol versions
(`v1.0 = 0x0100`), and a feature bitmap (bit 0 is time sync procedure 0, bit 1 is
procedure 1, bit 2 is LE coded PHY). It writes back the version and bitmap it selected.
Both version values are folded into the secure channel KDF, so a version downgrade makes
the two sides derive mismatched keys and the exchange fails. From there everything runs
over an L2CAP connection-oriented channel on that SPSM. The reader is the GAP peripheral
and GATT server, the phone is central and client, and BLE pairing is not required.

For initial sniffing, the fastest sanity check is `0xFFF2` service data with byte 7 bit 7
set: it confirms the reader side of discovery is alive within seconds.

## 4. Authentication, and the origin of the ranging key

The phone sends *Initiate Access Protocol* in the clear, and the reader runs one of two
paths:

- The **fast path** is symmetric. It works from a cached long-term key, `Kpersistent`,
  from an earlier transaction, and produces a 160-byte block of derived keys. The target
  key is URSK, at offset 128, 32 bytes long.
- The **standard path** is ECDH: AUTH0/AUTH1 with certificates, deriving a fresh 160-byte
  block (URSK still at offset 128) and caching a new `Kpersistent`. If the fast-path check
  fails, it falls through to the standard path.

Two properties here are load-bearing for the whole effort:

1. The URSK never goes out on the wire. Both sides derive it independently from the
   authentication, which cryptographically ties UWB ranging to a credential check that
   actually passed. It cannot be lifted from sniffed BLE traffic and injected or replayed.
2. After authentication, the reader sends an EXCHANGE command with a zero-length trigger
   (observed tagged `0x98`) that instructs the phone to load the URSK into its UWB chip.
   If the phone never sees that trigger, it responds with `URSK_Unavailable`, leaving
   tap-style behavior and no ranging. A build that authenticates cleanly but never emits
   `0x98` is the classic silent failure, and is worth learning to recognize on sight.

Glossary of the labels used: `Kpersistent` is the long-term symmetric seed, `BleSK`
guards the ranging control messages, `URSK` is the 32-byte ranging root above, and
`transaction_identifier` is a per-transaction value both sides feed into the crypto.

## 5. Time sync (using a BLE event to align the UWB clocks)

A battery-powered reader cannot leave its UWB receiver on continuously, so it must know
when the phone will transmit. The mechanism takes one BLE event both sides can observe and
maps it into each side's own UWB clock.

The reference instant is the anchor point of a BLE connection event: the on-air boundary
where the phone (central) transmits and the reader catches the first packet of the event,
so both radios observe the same moment. There are two procedures for selecting the event:

- **Procedure 0** happens at connection. The phone timestamps the very first connection
  event, sets `DeviceEventCount = 0`, and sends the time sync unprompted. This lands
  before any session key exists, so it is readable in the clear. It is the only time sync
  that can be snooped.
- **Procedure 1** is a resync. The reader initiates an `LE Set PHY` (`LL_PHY_REQ`), and
  the event carrying the phone's `LL_PHY_UPDATE_IND` becomes the reference. The reader
  uses this when it judges the phone's clock has drifted more than about 1 ms (for
  example, the last sync is older than ~150 s, or there has been no valid measurement
  within 10 s of it).

Each side reads its own UWB clock at that instant. The phone reports its value as
`UWB_Device_Time`; the reader keeps its own as `UWBVehicleTime` and never transmits it.
How each side maps the BLE anchor into its UWB clock is implementation-defined, which is
the difficult part covered in section 9.

The time sync message (phone to reader) carries: device event count (8 B), UWB device time
(8 B, µs resolution, 64-bit, scoped to the session and free to start at any value), UWB
device time uncertainty (1 B, log-encoded from 1 µs up to about an hour), a clock-skew
available flag, device max PPM (2 B), a success field (0/1/2), and a retry delay (2 B).

The math:

```
same event mapped both sides:  SyncOffset = UWB_Device_Time − UWBVehicleTime
different events:              SyncOffset = UWB_Device_Time
                                           + (VehicleEventCount − DeviceEventCount) × ConnectionInterval
                                           − UWBVehicleTime

reader opens block-i RX window at:
   local_listen(i) = UWB_Time0 + i × T_Block − SyncOffset            [µs, reader UWB clock]
                     ± ( 2^(Uncertainty/8) µs + skew·elapsed + reader mapping error )
```

`UWB_Time0` is in µs (the same 64-bit counter as `UWB_Device_Time`), and
`T_Block = N_RAN × 96 ms`. The phone's clock becomes undefined if the session suspends or
dies, or after 30 s with neither UWB nor BLE present. At least one successful time sync is
required before M1-M4. Once the first UWB packet lands, everything resyncs in-band far
tighter than BLE could manage. The BLE time sync only has to be good enough to place the
listen window in roughly the right spot, and the bar there is about 1 ms.

## 6. Ranging setup (M1-M4)

These four messages ride the BleSK channel as ranging service messages (message IDs:
M1=0, M2=1, M3=2, M4=3, suspend=4/5, resume=6/7). Either side can start the negotiation,
but M1 must not be sent while a session is already live.

| Msg | Dir | Carries |
|-----|-----|---------|
| **M1** | R→P | UWB config id(s), pulse shape combination, channel bitmask, **UWB Session Id** |
| **M2** | P→R | selected config/pulse/channel + SYNC code bitmask, **RAN Multiplier**, slot bitmask, hopping bitmask |
| **M3** | R→P | selected RAN Multiplier, **Chaps per Slot**, Number Responder Nodes, **Slots per Round** (≥ responders+4), SYNC code subset, hopping, **MAC Mode** (1 or 2 rounds/block + round offset) |
| **M4** | P→R | **STS Index0, UWB Time0**, Hop Mode Key, selected SYNC code index |

The key point: every negotiated parameter is load-bearing for the crypto, not just for the
radio config. This set is hashed into a value referred to here as the *SaltedHash*:

`ProtocolVersion ‖ ConfigId ‖ SessionId ‖ STS_Index0 ‖ ResponderNodes ‖ RAN_Multiplier ‖
SlotsPerRound ‖ ChapsPerSlot ‖ PulseShape`

and that hash feeds the ranging key KDF. So if the two sides disagree on any of it, they
derive different STS keys and never hear each other, even though the setup handshake
reported "success." That is the exact failure revisited in section 7 and section 10: the
negotiation "worked," and the radio is silent. (Aside: MAC mode set to 2 rounds per block
is the mechanism used to distinguish front-of-door from back-of-door.)

## 7. The UWB session

### Time grid (block → round → slot)

Time is carved into blocks, rounds, and slots:

```
RSTU    = 416 / 499.2 MHz ≈ 833.33 ns
T_Chap  = 1/3 ms = 400 RSTU
T_Slot  = N_Chap_per_Slot  × T_Chap                (Chaps-per-Slot from M3)
T_Round = N_Slot_per_Round × T_Slot                (Slots-per-Round from M3)
T_Block = N_RAN × 288 × T_Chap = N_RAN × 96 ms     (RAN Multiplier from M2/M3)
N_Round = T_Block / T_Round                        (derived)
Ranging rate = 10.416667 / N_RAN Hz

block_start(i)      = UWB_Time0 + i × T_Block
round_start(i,s)    = block_start(i) + s × T_Round
slot_start(i,s,n)   = round_start(i,s) + n × T_Slot
reader_local(…)     = (any of the above) − SyncOffset      [reader UWB clock]
```

Of the `N_Round` rounds in a block, only one or two are active (that is the MAC mode), and
which one is selected by the hopping sequence below.

### Who transmits in which slot (DS-TWR, one to many)

`N_Resp` is the number of responder nodes. The first pre-poll of a session goes out at
`UWB_Time0`.

| Slot | Packet | Format | Sender |
|---|---|---|---|
| 0 | Pre-POLL | SP0 (data) | phone: session id, **encrypted `Poll_STS_Index`**, block `i`, `Hop_Flag(i)`, `Round_Idx(i)` |
| 1 | POLL | SP3 (RFRAME) | phone |
| 2 … N_Resp+1 | Response_l | SP3 | one reader anchor each |
| N_Resp+2 | Final | SP3 | phone |
| N_Resp+3 | Final_Data | SP0 | phone: encrypted Poll/Final timestamps, `Hop_Flag(i+1)`, `Round_Idx(i+1)` |

So Slots-per-Round must be at least `N_Resp + 4`. The SP3 frames carry only a scrambled
timestamp sequence (the STS): no readable payload, and the distance falls out of RMARKER
timing across poll, response, and final. The SP0 frames are the opposite: plain data
(vendor OUI `0x4A191B`) wrapped in AES-CCM*. SP0 and SP3 are structurally different
packets, so rebuilding a single round means switching the radio between no-STS data RX/TX
and STS-only RFRAME RX/TX, and reloading the STS IV on every SP3 slot. One easy mistake:
the STS index increments on every slot whether or not it is used, so a skipped slot must
skip its IV too; never reuse it.

### STS index and the key ladder

```
STS_Index0 : random in [0 .. 2^30−1], pinned to block 0 / round 0 / slot 0
STS_Index(i, s) = STS_Index0 + (i·N_Round + s) · N_Slot_per_Round     s = Round_Idx(i)
   within a round:  Pre-POLL=base · POLL=base+1 · Response_l=base+2+l · Final=base+N_Resp+2 · Final_Data=base+N_Resp+3

URSK (32 B, from section 4)
 ├─ mUPSK1/mUPSK2   per session   → AES-CCM* ENC-MIC64 over the Pre-POLL payload (static all session)
 └─ mURSK           per session
     └─ URSK_KT     per active round   (keyed on that round's Pre-POLL STS index)
         ├─ dURSK   → generates the STS via the 802.15.4z DRBG
         └─ dUDSK   → encrypts the Final_Data timestamps
```

The finding that made responders tractable is *pre-poll recovery*. Because `mUPSK1` comes
straight off the URSK and stays static for the whole session, the responder can decrypt
the very first pre-poll with no ranging-round state set up yet. Decrypt it, read
`Poll_STS_Index`, subtract 1, derive `URSK_KT`, then `dURSK` and `dUDSK`, and the SP3 STS
is armed for the poll that lands one slot later. Between slot 0 and slot 1 that is about 4
AES-CMAC chains of work. This was the finding that mattered most: anchor every block on
the in-band pre-poll `Poll_STS_Index` rather than on the BLE time sync, and arm each slot
*before* the ~2 ms KDF/decrypt cost hits. That is the change that turned "setup is fine but
the radio is silent" into live distance reports.

### Hopping (which round is active)

Fully deterministic in the block index, so both sides can compute it with no radio
contact:

```
h_i = ( (((i + HOP_Key) & 0xFFFF)² mod 65521) × (N_Round − O_k) ) >> 16      (65521 = 2^16 − 15)
f_i = h_i + O_k                                                              (O_k = round offset, ≠ 0)
```

`HOP_Key` is the hop mode key from M4 (0 means no hopping). Block 0 is always unhopped, on
rounds 0 and `O_k`, and the sequence only takes over from block 1 onward. Test vector: with
`HOP_Key = 0xCC5DD79F` and `N_Round = 80`, blocks 1 and 2 land on rounds 62 and 37. Three
modes: none (fixed round), continuous (recompute `h_i` every block), and adaptive (stay put
while things work, hop on interference; the phone announces the next block's flag and round
inside Final_Data, and a missing Final_Data is assumed to be "hop"). Because the whole
thing is deterministic, losing a Final_Data costs nothing; both sides still land in the
same next round.

On the PHY side: HRP UWB high band, either channel 5 (6489.6 MHz) or channel 9
(7987.2 MHz), BPRF, with the preamble and pulse shape as negotiated in M2/M3/M4.

## 8. Session lifecycle and key lifetimes

Suspend and resume: either side can suspend to save power. In practice the phone usually
suspends while the door sits unlocked and resumes when the reader flips from unlocked back
to locked (or when the phone detects motion). A resume hands over a fresh `UWB_Time0` and
`STS_Index0`, giving a new grid without redoing M1-M4, and it only works if the reader
still holds a URSK that has not expired. A session paused while the door is unlocked is
normal, not a bug.

The URSK is dropped when any of these occur: the STS index reaches `2^31−1`, the STS index
is lost, a 12-hour TTL expires (counted from the first `dURSK` derivation at M4), or the
BLE link drops. After that, either side reports `URSK_Unavailable` and a fresh access
transaction is required, which is cheap because it can use the fast path. This is why
ranging reliably dies once the phone leaves BLE range, or after about 12 hours.

## 9. Open problem: pinning the reader's clock

This is the one genuinely implementation-defined seam from section 5: how the reader maps
the BLE connection-event anchor into its own UWB clock. On a split-core SoC (BLE
controller on the network core, host and UWB on the app core), this cannot be done in
software alone. The app core cannot read the controller's clock or the network-core timer,
and the ready-made radio-notification conversion path is fused off on this SoC family.

The approach adopted is a physical bridge rather than cross-core clock math:

```
controller "event-start task" ──▶ network-core GPIOTE pin (one edge / conn event) ──▶ DW3110 time-latch
BLE "anchor report" (event counter N, anchor_µs(N)) ──HCI/IPC──▶ app core
correlate by connection-event counter:  DW_time(edge_N) + L  ↔  anchor_µs(N)
```

`L` is a calibrated constant lead, covering radio ramp-up plus the latch offset. That pair
is exactly the `(UWBVehicleTime, anchor)` the section 5 formulas require, and because the
connection-event counter is a shared protocol counter, it also aligns with the phone's
`DeviceEventCount`. The DW3000 family provides `dwt_config_ostr_mode()` (one-shot timebase
reset over the sync pin) as the primitive that defines the epoch. Two items remain open:

- The anchor-report enable must live in the network-core image. Enabling it only from the
  app side is the prime suspect for a silent failure where the events never appear.
- The larger gap is obtaining the peer's time sync. The phone's `UWB_Device_Time` and
  device event count are consumed inside a closed protocol library and never surface. The
  procedure 0 time sync does arrive in the clear before any session keys exist, so it can
  be snooped on the receive path before the library consumes it; the later BleSK-encrypted
  syncs cannot. Without either a snoop hook or an upstream API addition, the reader has its
  `UWBVehicleTime` but nothing to subtract it from.

## 10. Field guide: observations and their explanations

| Observation | Reverse-engineered explanation |
|---|---|
| Phone never connects over BLE | Advert missing/wrong: `0xFFF2` absent, byte 7 bit 7 clear (no UWB build), or Dynamic Tag unresolvable (stale/changed reader identity). |
| BLE connects, tap-like unlock, no ranging | No common protocol version, or the `0x98` "URSK ready" trigger never sent → `URSK_Unavailable`. |
| M1 sent, phone answers "Setup Later" | Phone-side UWB busy/unavailable; wait for *Initiate Ranging Session* before resending M1. |
| M1-M4 complete, zero distance reports | Radio path: antenna, channel 5/9, missing time sync (wrong listen window), or a parameter mismatch → different SaltedHash → different STS (section 6). Watch for *Secure Ranging Over UWB Radio Failed*. |
| Ranging stops while unlocked, resumes at lock | Conformant suspend/resume for power saving (section 8), not a bug. |
| Everything dies after BLE drop / ~12 h | URSK lifetime rules (section 8); a fresh fast-path transaction is required. |

On instruments: a BLE sniffer alone yields everything through section 6 (discovery, the
versions, the message IDs, suspend/resume) with no UWB gear. Resolving section 7 needs a
UWB capture, or the radio's own per-frame diagnostics, which is what separates "setup
negotiated but the STS is mismatched" from "STS lines up but there is no RF link." The
single most useful question when distances vanish: is BLE still trading setup and ranging
control messages while the UWB side has gone silent? If so, the problem is in the
radio/parameter/STS path, not the control stack.

## Credits

- [@kormax](https://github.com/kormax/) for ideas on ECP and UWB.
- [@rednblkx](https://github.com/rednblkx/) for ideas on HomeKey.
- [@scottjg](https://github.com/scottjg/) for help with UWB chipset ideas.

## Questions

Open an issue on the repository for corrections or questions about this research.
