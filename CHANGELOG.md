# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0.0
the API and behavior may change in minor releases.

## [Unreleased]

### Added

#### nRF5340 DK (primary target)

- NFC ECP tap unlock (Express Mode, no Face ID) via the ST25R300 front end.
- BLE authentication and key agreement through the Nordic door-lock add-on.
- On-air Aliro ranging setup (M1-M4 codec).
- Credential-bound secure UWB ranging (DS-TWR, STS) implemented in firmware on a bare
  Qorvo DW3110, with no UWB coprocessor.
- Distance-gated unlock and relock with hysteresis, validated end to end on an nRF5340 DK
  against a live iPhone.

#### ESP32-S3 port (`ports/`)

- A standalone Aliro reader on ESP-IDF: BLE transport (NimBLE GATT + L2CAP CoC),
  credential authentication, key schedule, and URSK derivation, all reimplemented rather
  than delegated to a vendor library.
- The shared `modules/woz_uwb` ranging engine compiled unchanged for Xtensa behind a
  Zephyr-compat layer, with an ESP-IDF DW3000 SPI/GPIO backend.
- Negotiated M1-M4 ranging setup and live DS-TWR distance on the DW3000, tuned for the
  ESP32's real-time budget (DMA-disabled SPI, STS key cache, hot-path log throttling).
- A Matter door lock (`ports/esp32/apps/matter-lock`) that commissions into a Home app, provisions
  a key into Wallet, and hosts the reader on Matter's own NimBLE host.
- Approach unlock validated end to end on ESP32-S3 against a live iPhone: the Wallet
  unlock animation plays on approach and the bolt relocks on departure.
- Reader provisioning seam: NVS-backed reader identity and a credential trust store.

#### Project

- Host KAT test suite with a line-coverage floor, ASan/UBSan runs, patch-drift and
  shellcheck gates in CI.
- A second host test suite for the ESP32 port's crypto, wire codec, provisioning, and
  compat shim (`make test-port`, CI-gated).

[Unreleased]: https://github.com/asxeem/openaliro/commits/main
