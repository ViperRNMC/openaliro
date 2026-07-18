# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0.0
the API and behavior may change in minor releases.

## [Unreleased]

### Added

- NFC ECP tap unlock (Express Mode, no Face ID) via the ST25R300 front end.
- BLE authentication and key agreement through the Nordic door-lock add-on.
- On-air Aliro ranging setup (M1-M4 codec).
- Credential-bound secure UWB ranging (DS-TWR, STS) implemented in firmware on a bare
  Qorvo DW3110, with no UWB coprocessor.
- Distance-gated unlock and relock with hysteresis, validated end to end on an nRF5340 DK
  against a live iPhone.
- Host KAT test suite with a line-coverage floor, ASan/UBSan runs, patch-drift and
  shellcheck gates in CI.
- Experimental ESP32-S3 port under `ports/` (in progress, not validated end to end).

[Unreleased]: https://github.com/asxeem/openaliro/commits/main
