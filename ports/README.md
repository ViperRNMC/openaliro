# Ports

Ports of the UWB engine to targets other than the primary nRF5340 DK build. Each port is
additive: it reuses `modules/woz_uwb/src` and `deps/dw3000` unchanged and keeps all
target-specific code inside its own directory.

| Directory | What it is | Status |
|---|---|---|
| [`esp32-idf/`](esp32-idf/) | ESP32-S3 port on ESP-IDF: the shared engine behind a small Zephyr-compat layer, plus an ESP-IDF DW3000 backend | **Experimental.** DW3000 bring-up and the DS-TWR responder are validated on silicon (Phase 2); the credential-auth phase is in progress. Roadmap: [`docs/porting-esp32.md`](../docs/porting-esp32.md), current phase: [`docs/porting-esp32-phase3.md`](../docs/porting-esp32-phase3.md) |
| [`esp32s3/`](esp32s3/) | Early Zephyr-based ESP32-S3 spike (devicetree overlay + sample app) | **Superseded** by `esp32-idf/`; kept as a pin-mapping reference |

The primary target (nRF5340 DK, hardware-validated end to end) lives at the repository
root; see the top-level [README](../README.md).
