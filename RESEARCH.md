<p align="center">
  <img src="assets/hero.gif" width="640" alt="aliro unlock on ios" />
</p>

# reverse engineering a phone's uwb proximity unlock

just my notes on how a phone pops open a fixed reader when you walk up to it. the phone handles all
the chatter over bluetooth le, and then the two of them run an ultra wideband secure ranging exchange
so the reader actually knows how close you are before it unlocks. i really only care about two things
here: what actually hits the air, and what has to be true before ranging even starts.

the thing that kicked this off: nfc tap to unlock works totally fine, but the door just doesn't open
when you walk up. and honestly, tap working is a good clue, it means the ble transport, the
provisioning, and the creds are all healthy, so whatever's broken is uwb specific. the fall back to
nfc when uwb gets messed with is on purpose, which is exactly why tap kept working the entire time
ranging was dead.

---

> **educational, research, and study use only.** i wrote these notes purely for learning, independent
> research, and personal study of how ble + uwb secure ranging systems behave on the air. this is not
> an invitation to bypass, defeat, or gain unauthorized access to any access control system. only ever
> mess with hardware you own or have explicit, written authorization to test.
>
> i'm not affiliated with, endorsed by, or speaking on behalf of any company, standards body, or
> specification owner mentioned or alluded to here. all protocol specifications, standards, trademarks,
> product names, and other intellectual property referenced or implied stay the property of their
> respective owners, and every right, license, and disclaimer attached to them belongs to those
> entities, not to me.

---

## 1. what i'm working with

- the **phone** is the uwb initiator, aka the side sending the poll packets. it's got a u class uwb
  chip and it's on ios 26 or later.
- the **reader** is the fixed lock, and it's the uwb responder with a single anchor. the radio i'm
  poking at is a bare dw3000 family part (a dw3110) sitting on an nrf5340, no dedicated uwb
  coprocessor, so the mac, phy, and sts all get rebuilt in host firmware.
- everything protocol level rides on ble. uwb carries zero application data, it's purely the distance
  measurement. the unlock policy is entirely the reader's call: in the config i looked at, it unlocks
  at 150 cm or closer and relocks past 180 cm, so 30 cm of hysteresis.

## 2. the whole transaction, top to bottom

here's the full exchange in order. everything above the line is in the clear or auth protected;
everything below it is locked down under blesk.

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

once "access protocol completed" goes by, both sides drop the auth session keys. blesk is the one
that sticks around, and it's what guards all the ranging control traffic from here on out (time sync,
m1–m4, suspend/resume).

## 3. discovery and connection: all of this is sniffable

the reader just sits there beaconing `ADV_IND` on le 1m (and optionally coded s=2). the payload is
service data under 16 bit uuid `0xFFF2`. the fields i actually care about for re:

- byte 7, bit 7 is the "ble + uwb flow supported" flag. this is the one bit a uwb capable build sets
  and a control only build leaves at 0, and if it's 0 the phone won't even try to range. rest of that
  byte, for reference: bit 6 is the ble only flow, bits 4:3 are notification/error, bits 2:0 are the
  advertisement version.
- byte 8 is tx power. bytes 9–18 are a truncated reader group identifier plus a sub identifier.
- bytes 19–22 are the dynamic tag expiry as unix time, or `0xFFFFFFFF` if the reader has no clock.
  byte 23 is reserved.
- bytes 24–30 are the dynamic tag itself: the first 7 octets of
  `AES-128(GroupResolvingKey, pad ‖ AdvA ‖ expiry)`.

the phone scans passively and figures out which readers are "its own" by recomputing that dynamic tag
with every group resolving key it's holding. when one matches, it fires off a `CONNECT_IND`. flip
side: a reader that's gone stale or got reprovisioned is basically invisible to the phone even while
it's happily advertising, because the tag just doesn't resolve anymore.

the gatt and l2cap handshake goes like this. the phone reads a characteristic that hands back the
dynamic spsm (somewhere in `0x0080–0x00FF`), the supported protocol versions (`v1.0 = 0x0100`), and a
feature bitmap (bit 0 is time sync procedure 0, bit 1 is procedure 1, bit 2 is le coded phy). it
writes back the version and bitmap it picked. both version values get folded into the secure channel
kdf, so if anyone tries to downgrade the version the two sides derive mismatched keys and the whole
thing falls over. from there everything runs over an l2cap connection oriented channel on that spsm.
the reader is the gap peripheral and gatt server, the phone is central and client, and, worth noting,
you don't need ble pairing for any of this.

if you're just starting to sniff, the fastest sanity check is `0xFFF2` service data with byte 7 bit 7
set. that tells you the reader side of discovery is alive in a couple seconds.

## 4. auth, and where the ranging key comes from

the phone sends *initiate access protocol* in the clear, and the reader runs one of two paths:

- the **fast path** is symmetric. it works off a cached long term key, `Kpersistent`, from an earlier
  transaction, and produces a 160 byte block of derived keys. the one i'm after is ursk, which lives
  at offset 128 and is 32 bytes.
- the **standard path** is ecdh: auth0/auth1 with certs, deriving a fresh 160 byte block (ursk is
  still at offset 128) and caching a new `Kpersistent`. if the fast path check fails it just falls
  through to the standard path, no fuss.

two things here are load bearing for the whole effort:

1. the ursk never goes out on the wire. both sides derive it independently from the auth, which means
   uwb ranging is cryptographically tied to a credential check that actually passed. you can't just
   lift it off sniffed ble traffic and inject or replay it.
2. after auth, the reader sends an exchange command with a zero length trigger (i saw it tagged
   `0x98`) that basically means "load the ursk into your uwb chip." if the phone never sees that
   trigger, it comes back with `URSK_Unavailable` and you're stuck with tap style behavior and zero
   ranging. a build that auths clean but never emits `0x98` is the classic silent failure, and it's
   worth learning to spot on sight.

quick glossary, just the labels: `Kpersistent` is the long term symmetric seed, `BleSK` guards the
ranging control messages, `URSK` is the 32 byte ranging root from above, and `transaction_identifier`
is a per transaction value both sides feed into the crypto.

## 5. time sync (using a ble event to line up the uwb clocks)

a battery powered reader can't just leave its uwb receiver on all the time, so it has to know when the
phone is going to transmit. the trick is to take one ble event both sides can see and map it into each
side's own uwb clock.

the reference instant is the anchor point of a ble connection event: the on air boundary where the
phone (central) transmits and the reader catches the first packet of the event, so both radios see the
same exact moment. there are two procedures for picking which event:

- **procedure 0** happens at connection. the phone timestamps the very first connection event, sets
  `DeviceEventCount = 0`, and sends the time sync unprompted. this one lands before any session key
  exists, so it's readable in the clear. it's the only time sync you can snoop.
- **procedure 1** is a resync. the reader kicks off an `LE Set PHY` (`LL_PHY_REQ`), and the event
  carrying the phone's `LL_PHY_UPDATE_IND` becomes the reference. the reader reaches for this when it
  figures the phone's clock has drifted more than about 1 ms, say the last sync is older than ~150 s,
  or there's been no valid measurement within 10 s of it.

each side reads its own uwb clock at that instant. the phone reports its value as `UWB_Device_Time`;
the reader keeps its own as `UWBVehicleTime` and never sends it anywhere. how each side actually maps
the ble anchor into its uwb clock is left to the implementation, and that's the tricky part i get into
in section 9.

the time sync message (phone to reader) carries: device event count (8 B), uwb device time (8 B, µs
resolution, 64 bit, scoped to the session and free to start at any value), uwb device time uncertainty
(1 B, log encoded from 1 µs up to about an hour), a clock skew available flag, device max ppm (2 B), a
success field (0/1/2), and a retry delay (2 B).

the math:

```
same event mapped both sides:  SyncOffset = UWB_Device_Time − UWBVehicleTime
different events:              SyncOffset = UWB_Device_Time
                                           + (VehicleEventCount − DeviceEventCount) × ConnectionInterval
                                           − UWBVehicleTime

reader opens block-i RX window at:
   local_listen(i) = UWB_Time0 + i × T_Block − SyncOffset            [µs, reader UWB clock]
                     ± ( 2^(Uncertainty/8) µs + skew·elapsed + reader mapping error )
```

`UWB_Time0` is in µs (same 64 bit counter as `UWB_Device_Time`), and `T_Block = N_RAN × 96 ms`. the
phone's clock goes undefined if the session suspends or dies, or after 30 s with neither uwb nor ble
around. you need at least one successful time sync before m1–m4. once the first uwb packet actually
lands, everything resyncs in band far tighter than ble could ever manage. the ble time sync only has
to be good enough to drop the listen window in roughly the right spot, and the bar there is about 1 ms.

## 6. ranging setup (m1–m4)

these four messages ride the blesk channel as ranging service messages (message ids: m1=0, m2=1, m3=2,
m4=3, suspend=4/5, resume=6/7). either side can kick off the negotiation, but do not send m1 while a
session is already live.

| Msg | Dir | Carries |
|-----|-----|---------|
| **M1** | R→P | UWB config id(s), pulse shape combination, channel bitmask, **UWB Session Id** |
| **M2** | P→R | selected config/pulse/channel + SYNC code bitmask, **RAN Multiplier**, slot bitmask, hopping bitmask |
| **M3** | R→P | selected RAN Multiplier, **Chaps per Slot**, Number Responder Nodes, **Slots per Round** (≥ responders+4), SYNC code subset, hopping, **MAC Mode** (1 or 2 rounds/block + round offset) |
| **M4** | P→R | **STS Index0, UWB Time0**, Hop Mode Key, selected SYNC code index |

the thing to really keep in mind: every one of these negotiated parameters is load bearing for the
crypto, not just for the radio config. this set gets hashed into a value i've been calling the
*saltedhash*:

`ProtocolVersion ‖ ConfigId ‖ SessionId ‖ STS_Index0 ‖ ResponderNodes ‖ RAN_Multiplier ‖
SlotsPerRound ‖ ChapsPerSlot ‖ PulseShape`

and that hash feeds the ranging key kdf. so if the two sides disagree on any of it, they end up with
different sts keys and never hear each other, even though the setup handshake said "success." that's
the exact failure i keep coming back to in section 7 and section 10: the negotiation "worked," and the
radio is dead silent. (one aside: mac mode set to 2 rounds per block is the hook they use to tell
front of door from back of door.)

## 7. the uwb session itself

### time grid (block → round → slot)

time gets carved into blocks, rounds, and slots:

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

out of the `N_Round` rounds in a block, only one or two are actually active (that's the mac mode), and
which one is picked by the hopping sequence down below.

### who transmits in which slot (ds-twr, one to many)

`N_Resp` is the number of responder nodes. the first pre-poll of a session goes out right at
`UWB_Time0`.

| Slot | Packet | Format | Sender |
|---|---|---|---|
| 0 | Pre-POLL | SP0 (data) | phone: session id, **encrypted `Poll_STS_Index`**, block `i`, `Hop_Flag(i)`, `Round_Idx(i)` |
| 1 | POLL | SP3 (RFRAME) | phone |
| 2 … N_Resp+1 | Response_l | SP3 | one reader anchor each |
| N_Resp+2 | Final | SP3 | phone |
| N_Resp+3 | Final_Data | SP0 | phone: encrypted Poll/Final timestamps, `Hop_Flag(i+1)`, `Round_Idx(i+1)` |

so slots per round has to be at least `N_Resp + 4`. the sp3 frames carry nothing but a scrambled
timestamp sequence (the sts); no readable payload, and the distance falls out of rmarker timing across
poll, response, and final. the sp0 frames are the opposite: plain data (vendor oui `0x4A191B`) wrapped
in aes-ccm*. sp0 and sp3 are structurally different packets, so rebuilding a single round means
flipping the radio back and forth between no sts data rx/tx and sts only rframe rx/tx, and reloading
the sts iv on every sp3 slot. one thing that's easy to get wrong: the sts index bumps on every slot
whether it gets used or not, so if you skip a slot you have to skip its iv too, never reuse it.

### sts index and the key ladder

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

the bit that finally made responders tractable is what i've been calling *pre-poll recovery*. because
`mUPSK1` comes straight off the ursk and stays static the whole session, the responder can decrypt the
very first pre-poll with zero ranging round state set up yet. decrypt it, read `Poll_STS_Index`,
subtract 1, derive `URSK_KT`, then `dURSK` and `dUDSK`, and you've armed the sp3 sts for the poll that
lands one slot later. between slot 0 and slot 1 that's about 4 aes-cmac chains of work. this was the
finding that mattered most: anchor every block on the in band pre-poll `Poll_STS_Index` instead of on
the ble time sync, and arm each slot *before* the ~2 ms kdf/decrypt hits. that's the change that
turned "setup's fine but the radio is silent" into actual live distance reports.

### hopping (which round is active)

fully deterministic in the block index, so both sides can compute it with zero radio contact:

```
h_i = ( (((i + HOP_Key) & 0xFFFF)² mod 65521) × (N_Round − O_k) ) >> 16      (65521 = 2^16 − 15)
f_i = h_i + O_k                                                              (O_k = round offset, ≠ 0)
```

`HOP_Key` is the hop mode key from m4 (0 means no hopping). block 0 is always unhopped, on rounds 0 and
`O_k`, and the sequence only takes over from block 1 onward. test vector: with `HOP_Key = 0xCC5DD79F`
and `N_Round = 80`, blocks 1 and 2 land on rounds 62 and 37. three modes: none (fixed round),
continuous (recompute `h_i` every block), and adaptive (stay put while things are working, hop when
there's interference; the phone announces the next block's flag and round inside final_data, and if a
final_data goes missing it's just assumed to be "hop"). since the whole thing is deterministic, losing
a final_data costs you nothing; both sides still land in the same next round.

on the phy side: hrp uwb high band, either channel 5 (6489.6 MHz) or channel 9 (7987.2 MHz), bprf,
with the preamble and pulse shape being whatever got negotiated back in m2/m3/m4.

## 8. session lifecycle and how long the keys live

suspend and resume: either side can suspend to save power. in practice the phone usually suspends
while the door's just sitting unlocked and resumes when the reader flips from unlocked back to locked
(or when the phone feels itself moving). a resume hands over a fresh `UWB_Time0` and `STS_Index0`, so
you get a brand new grid without redoing m1–m4, and it only works if the reader is still holding a ursk
that hasn't expired. a session that's paused while the door's unlocked is totally normal, not a bug.

the ursk gets dropped when any of these happen: the sts index hits `2^31−1`, the sts index gets lost, a
12 hour ttl runs out (counted from the first `dURSK` derivation back at m4), or the ble link drops.
after that, either side reports `URSK_Unavailable` and you need a fresh access transaction, which is
cheap since it can use the fast path. this is why ranging reliably dies once you walk out of ble range,
or after about 12 hours.

## 9. the open problem: pinning the reader's clock

this is the one genuinely implementation defined seam from section 5: how the reader maps the ble
connection event anchor into its own uwb clock. on a split core soc (ble controller on the network
core, host and uwb on the app core) you can't do this in software alone. the app core can't read the
controller's clock or the network core timer, and the ready made radio notification conversion path is
fused off on this soc family.

what i landed on is a physical bridge instead of trying to do cross core clock math:

```
controller "event-start task" ──▶ network-core GPIOTE pin (one edge / conn event) ──▶ DW3110 time-latch
BLE "anchor report" (event counter N, anchor_µs(N)) ──HCI/IPC──▶ app core
correlate by connection-event counter:  DW_time(edge_N) + L  ↔  anchor_µs(N)
```

`L` is a calibrated constant lead, covering radio ramp up plus the latch offset. that pair is exactly
the `(UWBVehicleTime, anchor)` the section 5 formulas want, and since the connection event counter is
a shared protocol counter, it also lines up with the phone's `DeviceEventCount`. the dw3000 family
gives you `dwt_config_ostr_mode()` (one shot timebase reset over the sync pin) as the primitive that
defines the epoch. two things i still haven't fully closed out:

- the anchor report enable has to live in the network core image. turning it on only from the app side
  is my prime suspect for a silent failure, where the events just never show up.
- the bigger gap is getting the peer's time sync in. the phone's `UWB_Device_Time` and device event
  count get swallowed inside a closed protocol library and never surface. the procedure 0 time sync
  does arrive in the clear before any session keys exist, so it can be snooped on the receive path
  before the library eats it; the later blesk encrypted syncs can't. without either a snoop hook or an
  upstream api addition, the reader has its `UWBVehicleTime` but nothing to subtract it from.

## 10. field guide: what i check and what it's telling me

| Observation | Reverse engineered explanation |
|---|---|
| Phone never connects over BLE | Advert missing/wrong: `0xFFF2` absent, byte 7 bit 7 clear (no UWB build), or Dynamic Tag unresolvable (stale/changed reader identity). |
| BLE connects, tap like unlock, no ranging | No common protocol version, or the `0x98` "URSK ready" trigger never sent → `URSK_Unavailable`. |
| M1 sent, phone answers "Setup Later" | Phone side UWB busy/unavailable; wait for *Initiate Ranging Session* before resending M1. |
| M1–M4 complete, zero distance reports | Radio path: antenna, channel 5/9, missing time sync (wrong listen window), or a parameter mismatch → different SaltedHash → different STS (section 6). Watch for *Secure Ranging Over UWB Radio Failed*. |
| Ranging stops while unlocked, resumes at lock | Conformant suspend/resume for power saving (section 8), not a bug. |
| Everything dies after BLE drop / ~12 h | URSK lifetime rules (section 8); a fresh fast phase transaction is required. |

on instruments: a ble sniffer alone gets you everything through section 6 (the discovery bit, the
versions, the message ids, suspend/resume) with zero uwb gear. to resolve section 7 you need a uwb
capture, or the radio's own per frame diagnostics, which is what separates "setup negotiated but the
sts is mismatched" from "sts lines up but there's no rf link." the single most useful question when
the distances vanish: is ble still trading setup and ranging control messages while the uwb side has
gone silent? if so, the problem is in the radio/parameter/sts path, not the control stack.

## credits

- [@kormax](https://github.com/kormax/) for ideas on ecp and uwb.
- [@rednblkx](https://github.com/rednblkx/) for ideas on homekey.
- [@scottjg](https://github.com/scottjg/) for helping with uwb based chipset ideas.

## contact

reach out to me at [asxeem@pm.me](mailto:asxeem@pm.me).
