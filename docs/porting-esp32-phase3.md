# ESP32-S3 port — Phase 3: the credential auth (deriving the ranging key)

**Status: complete and hardware-validated.** Every step below has since run against a
live iPhone: the transaction completes, the derived ranging key is correct, and the
ranging setup it feeds ends in an approach unlock. This page describes the design and why
each piece exists; the traps hit proving it are in
[`../ports/docs/esp-32-gotchas.md`](../ports/docs/esp-32-gotchas.md) §4 and §5.

Prerequisite reading: [`porting-esp32.md`](porting-esp32.md) (roadmap and retrospective)
and [`protocol-research.md`](protocol-research.md) (the reverse-engineered protocol
notes; this doc stays at that same disclosure level and cites no external
specifications).

## Where Phase 2 left off

The BLE transport and reader scaffold were already green on silicon: advertising the
service (`0xFFF2`), serving the SPSM/version GATT read, bringing up the L2CAP CoC server
on the SPSM, and coexisting with the UWB responder on one ESP32-S3. Components:
`ports/esp32-idf/components/aliro_ble` (transport) and `.../aliro_reader` (the
session/transaction scaffold, with the crypto handshake seam still stubbed).

## The boundary finding (why this phase existed at all)

Phase 3 is the BLE credential-auth transaction that yields the 32-byte **URSK**, the
ranging root (see [`protocol-research.md`](protocol-research.md) §4). Before this phase it
was the one layer the project had never reimplemented:

- **Already reimplemented and already ported** (via `modules/woz_uwb`, compiled
  unchanged): the entire UWB side — the ranging engine, the ranging-setup message
  exchange, and the STS key ladder **from the URSK down** (`ccc_derive_mupsk1/2`,
  `ccc_derive_mursk`, `ccc_derive_ursk_kt`, `ccc_derive_dursk/dudsk`,
  `ccc_derive_salted_hash` in `modules/woz_uwb/src/ccc`).
- **The gap**: Initiate Access → auth exchange → secure channel → **URSK derivation**. In
  the reference design a closed vendor library handles this
  ([`protocol-research.md`](protocol-research.md) §9 calls it "a closed protocol
  library"), and every crypto function in this tree took the URSK as an *input* rather
  than deriving it.

That library is an ARM binary and cannot link on the Xtensa S3. There was no way around
it: a standalone ESP32 reader had to derive the URSK itself. Everything downstream of the
URSK was already done, and pure UWB bench testing via the canned-URSK path in `main.c`
still needs none of it.

## What Phase 3 built (reverse-engineered, per protocol-research.md §4)

- **Transport / framing:** L2CAP CoC (already up), one APDU per SDU.
- **Flow:** a standard path (ephemeral ECDH + mutual signatures) and a fast path off
  a cached long-term key, both producing the 160-byte derived block with the URSK at
  offset 128 (`docs/protocol-research.md` §4).
- **Crypto suite** (all available in ESP-IDF's mbedTLS-PSA): NIST P-256; ECDH; ECDSA
  P-256 with SHA-256; SHA-256; AES-256-GCM; and the two-stage KDF (a single-block
  X9.63-style step then HKDF-SHA-256) that expands the derived block.
- **Handoff:** the derived URSK + the negotiated ranging parameters land on the
  existing engine entry point `woz_uwb_start_aliro(cfg)`
  (`modules/woz_uwb/src/facade/woz_uwb_facade.h`): `session_id`, `channel`,
  `sync_code_index`, `slot_duration_rstu`, `block_duration_ms`, `slot_per_round`,
  `sts_index0`, `uwb_time_us`, `ursk[32]`.

### Provisioning dependency

Auth cannot complete without a provisioned identity: a reader identifier, a reader P-256
key pair, the reader-group key material, and the credential-issuer public keys that
validate the phone's credential. Without a reader identity and issuer keys matching a
credential already in the phone's wallet, no handshake completes, however correct the
code. Phase 4 supplies these over Matter; step 3.4 below supplies a fixed dev identity so
the transaction is drivable at a bench before that.

## Phase 3 steps

Each step below is implemented, host-KAT'd where it is host-testable, and confirmed on
silicon against a live phone.

- **3.1 — the key schedule.** `aliro_crypto`
  (`ports/esp32-idf/components/aliro_crypto`): a portable SHA-256 / HMAC / HKDF /
  X9.63-KDF core (compiled identically on host and target) plus an mbedTLS-PSA
  backend for AES-256-GCM and P-256 ECDH/ECDSA. On top of that, the key schedule:
  - stage 1 `Z = SHA-256( ecdh_shared(32) ‖ 0x00000001 ‖ txid(16) )` (single-block
    X9.63), then stage 2 `block160 = HKDF-SHA256(salt = transcript, IKM = Z,
    info = devicePubX(32), L = 160)`, with the **URSK = block[128:160]** and the two
    directional session keys split from the low segments;
  - the `Kpersistent` / cryptogram-key single-block derivations (same HKDF, different
    salt / label);
  - the AES-256-GCM secure channel: 12-byte nonce = 8-byte big-endian direction
    (0 = seal, 1 = open) ‖ 4-byte big-endian per-direction counter, separate
    non-wrapping counters.

  Host KATs (`ports/esp32-idf/test/test_aliro_crypto.c`, in `run.sh`) pass against
  FIPS-180-4, RFC 4231, RFC 5869, a GCM spec vector, and cross-check the schedule
  wiring; the whole component also builds and links into the firmware.

  The salt transcript (`aliro_salt_build`) was the last provisional piece and is now
  resolved: the two open sub-fields turned out to be the reader's *signing* public key X
  (not the device ephemeral key — that was the bug) and an interface byte distinguishing
  BLE from NFC. See the gotchas log §4.3. The AUTH1 tag does not decrypt until the salt
  is byte-exact, so a green AUTH1 is the proof.
- **3.2 — the wire codec.** `aliro_apdu`
  (`modules/woz_aliro/src/aliro_apdu.{c,h}`): single-byte-tag
  BER-TLV plus the AUTH0/AUTH1 command builders, the ECDSA authentication-data
  transcript (the exact signed bytes, with the reader/device usage domain
  separators), the AUTH0/AUTH1 response parsers, the EXCHANGE command + the
  zero-length `98 00` URSK-ready trigger, and the 4-byte L2CAP envelope
  (`[type&0x3F][opcode][len_be16]`). Host KAT: `test/test_aliro_apdu.c` in `run.sh`.
  `aliro_reader` now drives the transaction (AUTH0 → AUTH1 → EXCHANGE), running
  ECDH + the key schedule to derive the URSK, replying via `aliro_ble_send`, with
  heavy diagnostic logging.
- **3.3 — superseded by 3.5.** On a completed handshake the
  reader originally started the responder with **canned** ranging parameters
  (`woz_uwb_start_aliro`). 3.5 replaces that canned hand-off with the real M1-M4
  negotiation, so the params are now negotiated with the peer rather than fixed.
- **3.4 — the provisioning seam.** The provisioning seam
  (`modules/woz_aliro/src/aliro_prov.c` + `modules/woz_aliro/include/aliro_prov.h`,
  with the storage backend in `ports/esp32-idf/components/aliro_reader/aliro_prov_nvs.c`):
  the reader identity (a stable reader identifier + P-256 signing key) and a
  credential **trust store**, NVS-backed with a clearly-marked, fixed **dev
  identity** fallback so the transaction is drivable at bench before Phase-4 Matter
  provisioning writes a real identity. Split like `aliro_crypto`: the dev default +
  blob (de)serialisation + trust logic are portable and host-KAT'd
  (`test/test_aliro_prov.c` in `run.sh`); the NVS load/store is target-only. The
  reader now loads identity+trust at start (replacing the per-boot random dev key,
  which changed the reader identity every reboot), signs with the provisioned key,
  and gates on a trust check after verifying the device signature: a raw-key
  allowlist (the interim seam where real issuer-chain validation plugs in), with a
  dev-open policy (accept + loud warning) only while the dev identity has no anchors.
  Two bench console commands drive it: `aliro-prov` (show identity + trust store +
  last-presented credential) and `aliro-trust` (persist the last-presented
  credential key as trusted). Phase-4 Matter `SetAliroReaderConfig` writes the same
  NVS blob to supply a real identity + issuer trust.
- **3.5 — the ranging setup.** The post-auth Aliro UWB
  ranging-setup (**M1-M4**), wiring the reader to the engine's reader adapter/session
  (`modules/woz_uwb/src/aliro` `aliro_uwb_adapter` + `aliro_uwb_session`), which were
  compiled into the ESP32 image but had **no caller**. New
  `modules/woz_aliro/src/aliro_ranging.{c,h}`: on a completed
  credential-auth it creates a reader session bound to the derived URSK, emits **M1**
  over the L2CAP channel, routes inbound **M2/M4** into `aliro_uwb_session_message_handle`,
  and lets the engine negotiate the parameters and start the DW3000 responder itself
  (`cherry_ccc_shim` maps the CCC session-start onto `woz_uwb_start_aliro`, so the
  reader no longer calls it directly and the canned `k_ranging` table is gone). The
  engine's transmit callback frames straight to `aliro_ble_send`; the whole lifecycle
  (create/feed/teardown + the transmit/event callbacks) is synchronous on the BLE-host
  task, and the DW3000 is single-session. The `woz_uwb` component now exposes
  `aliro/include` (the `aliro_uwb_adapter/` + `cherry/` headers) publicly for the reader.
  This is a real engine-integration, not host-testable: it compiles and links now, and
  only fully verifies against a phone at bench.

## What verification actually means here

Three different kinds of evidence back this phase, and they are not interchangeable:

- **Host KATs** (`ports/esp32-idf/test/run.sh`) pin the key schedule against published
  vectors, the wire codec byte-for-byte, and the provisioning logic. The crypto core
  compiles host-identical to target, which is what lets a host result say anything about
  on-target behavior.
- **`verify_port.sh`** proves the firmware builds and links with the STS substitution
  seam intact.
- **A live iPhone** is the only thing that proves interop, and it is the bar this phase
  now clears. Everything before that bar built and linked cleanly while still being
  wrong: the salt transcript, the GCM counter start, the ranging channel split, and the
  session id were each a clean build that a phone rejected. Build success proves nothing
  about this layer.

Phase 4 supplies the missing piece for a real deployment. Until a controller writes a
reader identity through `SetAliroReaderConfig`, the reader runs on its fixed dev identity
with a dev-open trust policy: it accepts the presented credential and logs a warning.
That is a bench seam, not a security control.

The heavy diagnostic logging at each step is deliberate — it is what made the bring-up
tractable. Note that on the ranging hot path it later had to be throttled, because a
blocking log line inside a 2 ms deadline is itself a real-time bug.
