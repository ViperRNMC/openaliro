# `aliro_crypto` — key schedule and secure channels

The cryptography the reader needs to turn a credential exchange into a ranging key, and
to seal and open everything that follows.

This component exists because the reference design does not derive the ranging key in
portable code — a closed vendor library does, and it ships only as an ARM binary. On
Xtensa there was nothing to link, so this is a from-scratch reimplementation.

## Structure

The split is deliberate and it is what makes the tests meaningful:

| File | Role |
|---|---|
| `modules/woz_aliro/src/aliro_hash.c` | SHA-256, HMAC, HKDF, and the X9.63 KDF, in portable C11 with no dependencies. Compiles **identically on host and target**. |
| `modules/woz_aliro/src/aliro_crypto.c` | The schedule built on those primitives: the key block, the split, the secure channels, the salt transcripts. Also portable. |
| `modules/woz_aliro/src/aliro_prim_psa.c` | The AEAD/EC backend — AES-256-GCM, P-256 ECDH/ECDSA, and randomness over the PSA Crypto API (mbedTLS-PSA here, nrf_security on the nRF). |
| `../../test/aliro_prim_host.c` | A compact host double of the same interface, so the KATs run without PSA. |

None of these are ESP32-specific, so they live in `modules/woz_aliro` and are shared with
the nRF build rather than duplicated. This component is only the ESP-IDF build wiring
around them.

Because the first two are host-identical to target, a passing host KAT is a statement
about what the firmware actually computes, not an approximation of it. The backend is the
only part that differs, and it is the part with no protocol logic in it.

## What it computes

- **The key block.** A single-block X9.63 step over the ECDH shared secret and the
  transaction id, then one HKDF-SHA256 expand to 160 bytes. The ranging key and the
  BLE-channel key are slices of that one block. Because it is one expand, if
  authentication succeeds at all then the ranging key is byte-identical on both sides —
  a fact that ruled out a lot of false leads during ranging debug.
- **The salt transcript.** The HKDF salt is a structured transcript, and the tag does not
  verify until it is byte-exact. Two of its fields were the hardest single thing to pin
  down in the whole port; see `ports/docs/esp-32-gotchas.md` §4.3.
- **Two independent AES-256-GCM channels.** The credential-auth channel and the ranging
  channel are separate, with separate keys and separate counters, both starting at 1.
  Assuming they were one channel, or that counters start at 0, produces clean builds that
  fail a tag with no other signal.

## Tests

`../../test/run.sh` checks the primitives against published vectors (FIPS 180-4, RFC
4231, RFC 5869, a GCM vector) and the schedule composition on top. Run it before
believing any change here.

A further known-answer test reproduces a published worked example byte-exact. It is not
committed, because those vectors are not ours to redistribute.

## Depends on

`mbedtls` (target backend only).
