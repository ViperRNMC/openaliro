# ESP32-S3 port — Phase 3: the credential auth (deriving the ranging key)

Status recorded 2026-07-18. Prerequisite reading: `docs/porting-esp32.md` (roadmap)
and `RESEARCH.md` (the reverse-engineered protocol notes; this doc stays at that
same disclosure level and cites no external specifications).

## Where Phase 2 left off (hardware-validated)

The BLE transport + reader scaffold is built, flashed, and green on silicon:
advertises the service (`0xFFF2`), serves the SPSM/version GATT read, brings up the
L2CAP CoC server on the SPSM, and coexists with the UWB responder
(`woz_uwb_start_aliro() = 0`) on one ESP32-S3. Components: `ports/esp32-idf/
components/aliro_ble` (transport) and `.../aliro_reader` (session/transaction
scaffold, with the crypto handshake seam stubbed).

## The boundary finding (what is and isn't reimplemented)

Phase 3 is the BLE credential-auth transaction that yields the 32-byte **URSK** (the
ranging root; see `RESEARCH.md` §4). This is the one layer this project has **not**
reimplemented:

- **Reimplemented, and already ported to ESP32** (via `modules/woz_uwb`, compiled
  unchanged): the entire UWB side — the ranging engine, the ranging-setup message
  exchange, and the STS key ladder **from the URSK down** (`ccc_derive_mupsk1/2`,
  `ccc_derive_mursk`, `ccc_derive_ursk_kt`, `ccc_derive_dursk/dudsk`,
  `ccc_derive_salted_hash` in `modules/woz_uwb/src/ccc`).
- **Not implemented here**: Initiate Access → auth exchange → secure channel →
  **URSK derivation**. In the reference design this step is handled by a closed
  vendor library (`RESEARCH.md` §9 calls it "a closed protocol library"); every
  crypto function in this tree takes the URSK as an *input* rather than deriving it.

**Implication for ESP32:** that closed library is an ARM binary and can't be linked
on the Xtensa S3, so for a standalone ESP32 reader the auth → URSK step must be
reimplemented. Everything downstream of the URSK is already done. For pure UWB bench
testing, the canned-URSK path (`main.c`) needs none of this.

## What Phase 3 has to build (reverse-engineered, per RESEARCH.md §4)

- **Transport / framing:** L2CAP CoC (already up), one APDU per SDU.
- **Flow:** a standard path (ephemeral ECDH + mutual signatures) and a fast path off
  a cached long-term key, both producing the 160-byte derived block with the URSK at
  offset 128 (`RESEARCH.md` §4).
- **Crypto suite** (all available in ESP-IDF's mbedTLS-PSA): NIST P-256; ECDH; ECDSA
  P-256 with SHA-256; SHA-256; AES-256-GCM; and the two-stage KDF (a single-block
  X9.63-style step then HKDF-SHA-256) that expands the derived block.
- **Handoff:** the derived URSK + the negotiated ranging parameters land on the
  existing engine entry point `woz_uwb_start_aliro(cfg)`
  (`modules/woz_uwb/src/facade/woz_uwb_facade.h`): `session_id`, `channel`,
  `sync_code_index`, `slot_duration_rstu`, `block_duration_ms`, `slot_per_round`,
  `sts_index0`, `uwb_time_us`, `ursk[32]`.

### Provisioning dependency (Phase 4 must supply first)

Before any auth can complete the reader needs a provisioned identity: a reader
identifier, a reader P-256 key pair, the reader-group key material, and the
credential-issuer public keys that validate the phone's credential. Without a reader
identity + issuer keys matching a credential already in the phone's wallet, no
handshake completes — however correct the code.

## Phase 3 plan (incremental, commit per step)

- **3.1** `aliro_crypto` on mbedTLS-PSA: P-256 ECDH, ECDSA, AES-256-GCM, SHA-256, and
  the KDF chain, plus the 160-byte derived-block / URSK schedule — with **host
  known-answer tests**. Verifiable now, no hardware.
- **3.2** APDU secure channel + the auth state machine, wired into `aliro_reader`'s
  `on_data` / `aliro_ble_send` seam.
- **3.3** completion + URSK → `woz_uwb_start_aliro(cfg)` (replacing the canned URSK).
- **3.4** (with Phase 4) provision the reader identity/keys so a real phone can auth.

**Verification reality:** end-to-end needs a provisioned phone (Phase 4). Only the 3.1
crypto/URSK unit is verifiable now, against known-answer vectors.
