# Host tests for the ESP32-S3 port

```bash
ports/esp32/test/run.sh
```

Plain `cc`, no ESP-IDF, no hardware. Every bug in the gotchas log built cleanly first, so
a green build is not evidence of anything on this target — these suites are.

| Suite | What it pins |
|---|---|
| `test_port_headers.c` | The `woz_port.h` platform contract and the pure `woz_bytes.h` / `woz_util.h` helpers behave as specified |
| `test_aliro_crypto.c` | SHA-256, HMAC, HKDF, X9.63 and AES-GCM against published vectors, then the key-schedule composition and secure-channel counters on top |
| `test_aliro_apdu.c` | The wire codec byte-for-byte: command builders, signed-data transcripts, response parsers, the L2CAP envelope |
| `test_aliro_prov.c` | Reader identity serialization and the trust store, including malformed-blob rejection |
| `test_lock_led.c` | The bolt-state LED policy, including that the two unlock paths stay distinguishable |

`aliro_prim_host.c` is a compact host double of the crypto backend interface, so the KATs
run without PSA. It is test scaffolding, never linked into firmware.

The crypto core and the wire codec compile host-identical to target. That is what makes a
host result a statement about on-target behavior rather than an approximation.

## `verify_port.sh`

Run last by `run.sh`, and skipped with a notice if `idf.py` is not on `PATH`. It builds
the firmware and then checks four things a compile alone would not catch: the `--wrap`
link seam survived, the wrapper symbols are defined and the engine still references the
wrapped symbol, the excluded diagnostic sources stayed out of the build, and the app
still fits its partition.

## CI

These suites are not wired into any workflow — CI builds and tests the nRF side only.
Run them yourself before pushing a change to `ports/`.
