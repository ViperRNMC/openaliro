# openaliro on ESP32-S3: porting roadmap and retrospective

**Status: shipped and hardware-validated.** Phases 1 through 4 are done; approach unlock
runs end to end on an ESP32-S3 against a live iPhone. Phase 5 (the NFC tap path) was not
attempted. The code lives in [`ports/esp32`](../ports/esp32) (components, plus the
bench-reader and matter-lock apps).

This document keeps the original plan, written before any ESP32 code existed, and marks
what the plan got right and wrong. The plan's own estimates are left unedited so the
comparison is honest. For the bring-up detail that the plan could not have predicted, see
[`docs/esp32-gotchas.md`](esp32-gotchas.md).

Target hardware: ESP32-S3-WROOM (N16R8, 16 MB flash / 8 MB octal PSRAM, Wi-Fi + BLE, no
802.15.4) plus a DWM3000EVB for UWB. No NFC reader was sourced.

## 0. What the plan got wrong

Worth stating up front, because it is the useful part of a retrospective:

- **The BLE transport was not the dominant rewrite.** The plan called ~650 lines of
  NimBLE GATT + L2CAP the concentrated new work. It was the easy part. The real work was
  the layer the plan assumed it could keep: the credential authentication and ranging-key
  derivation, which the reference delegates to a closed ARM-only library that cannot link
  on Xtensa. That had to be reimplemented from the wire up — key schedule, two separate
  GCM channels, wire codec, reader identity.
- **The plan had no line item for real-time behavior.** The DS-TWR responder was treated
  as "already done, just recompile it." The logic was indeed already correct — the nRF
  proves it — but the ESP32 has slower SPI and jitterier callback dispatch, and arming
  each frame inside a 2 ms slot took a separate campaign: DMA-disabled SPI, an STS key
  cache, and throttling a per-round log that was starving the ISR task.
- **The reference source did not have to be a Nordic-licensed study target.** The
  provenance discipline was kept, but the decisive facts came from disassembling the
  vendor binary and cross-checking readable reference source, not from reading the
  add-on's C++.

## 1. Summary

This is not a spec reverse-engineering project. The DIY Nordic lock already provisions a credential
into Apple Wallet today, and the reader that does it is readable, layered Nordic source: the
`subsys/aliro/` subsystem (about 12 components, ~4.4k lines of the relevant parts) plus the
app-side `src/aliro/` (~5.1k lines). The port is reimplementing that ~10k lines of C++ on ESP-IDF,
tiered by how portable each piece is. The genuine rewrite is small and concentrated: ~650 lines of
BLE transport (Zephyr Bluetooth to NimBLE). Everything else is either common-SDK, PSA-portable, or
already seam-defined.

Decision: **ESP-IDF + esp-matter**. First real engineering is the UWB engine on silicon (Phase 1,
fully unblocked). The old "will Apple provision a DIY lock" gate is closed by the working nRF.

Constraint held throughout: **the working nRF5340DK stays untouched** (observe-only reference; no
reflash, reconfigure, reprovision, or key readout, and no connection to it without sign-off).

Licensing overlay (important): the add-on is `LicenseRef-Nordic-5-Clause`, which restricts use to
Nordic devices. The source is a reference to study, not to lift onto ESP32. A publishable port must
be clean-room reimplemented (reimplement behavior, do not copy files), matching the provenance
discipline already used in this project.

## 2. Port map (grounded in the add-on source)

> Historical. The tier assignments below held up for Matter, crypto primitives, and
> storage. They missed the closed-library boundary described in §0.

The engine seam is confirmed. Nordic's Aliro reader talks to UWB only through the `UltraWideBand`
C++ interface (`subsys/aliro/uwb/.../uwb.h`), and this repo's engine already implements it (the
in-repo `ports/nrf5340dk/patches/custom_impl-uwb.patch` fills `_ConfigureRangingSession(sessionId,
ursk, ...)` to call `aliro_uwb_session_set_ursk`, and `_HandleBleMessage(...)` to route the Aliro
UWB messages, backed by `woz_uwb_facade`). So on ESP32 the engine keeps implementing the same
interface; the reader above it is what gets rebuilt.

Portability tiers (line counts are the Nordic reference, not a copy target):

| Component | ~Lines | Tier | ESP-IDF plan |
|---|---|---|---|
| door_lock_delegate + Matter Aliro cluster | 330 + SDK | A: common SDK | connectedhomeip is shared by NCS and esp-matter, so the Matter/provisioning side ports with modest change. This is why DIY Wallet provisioning already works. |
| crypto_utils + app crypto seam | 327 + part of 1311 | A: PSA-portable | Uses the PSA Crypto API (`psa/crypto.h`). ESP-IDF mbedTLS provides PSA; port at the API level, backend moves CC312 -> mbedTLS. |
| aliro_service (orchestration) | 975 | B: logic + seams | Portable C++; swap Zephyr workqueue/storage seams. |
| access_manager, disambiguation, access_document | 2171 + 352 + hdr | B: logic + seams | Credential matching / access decision logic; mostly portable. |
| reader_storage, storage, kpersistent_manager | 1374 + 654 + 329 | B: storage seam | Zephyr settings/NVS -> ESP-IDF NVS. |
| aliro_workqueue, time_utils, lock_sim | 83 + 95 + 245 | B: small glue | Zephyr workqueue/time -> FreeRTOS/esp_timer; lock_sim trivial. |
| gatt_server + l2cap_server | 354 + 300 | C: real rewrite | Zephyr `bluetooth/gatt.h` + L2CAP CoC -> NimBLE GATT + L2CAP CoC. The Aliro BLE transaction transport. The dominant rewrite, but only ~650 lines. |
| uwb/custom_impl over woz_uwb | small + engine | D: done + engine port | Keep the `UltraWideBand` impl; port the engine per section 4. Do NOT port `uwb/qm35_impl` (the Qorvo coprocessor path, ~3k lines, unused here). |
| platform/nfc (RFAL + ECP) | 647 | E: use esp-aliro | Espressif ships **esp-aliro** (github.com/espressif/esp-aliro), first-party Aliro-over-NFC on ESP32, license-clean. Use it for the tap applet instead of a clean-room RFAL reimplementation. NFC-only today; BLE + UWB are on its roadmap. |

Take-away: the frightening parts (provisioning, crypto) are the portable ones (common
connectedhomeip; PSA). The concentrated new work is BLE transport on NimBLE.

## 3. Decision: ESP-IDF, not Zephyr

Apple Home + Wallet requires Matter (no sideload path to Wallet). On ESP32-S3 (no 802.15.4) that is
Matter over Wi-Fi, which Apple Home accepts; the transport does not matter to Apple (the nRF used
Thread). esp-matter is the only viable ESP32 Matter path, and it shares connectedhomeip with NCS,
so the door_lock_delegate + Aliro cluster (Tier A) carries over. ESP-IDF also gives native NimBLE
(the L2CAP transaction), mbedTLS-PSA (the crypto), NVS (storage), FreeRTOS core-pinning, and PSRAM.
Espressif now also ships first-party Aliro (esp-aliro, NFC today; BLE + UWB roadmapped), which only
strengthens ESP-IDF over Zephyr.

The decision held. Everything the plan expected from ESP-IDF was there: NimBLE's L2CAP
CoC carried the transaction, mbedTLS-PSA covered the crypto, NVS covered storage, and
core-pinning mattered for the ranging task.

### Toolchain

- Use the ESP-IDF version esp-matter recommends, and install esp-matter on top of that
  same tree. The two ports share one toolchain. **Nothing in this repository pins a
  version**: the Makefiles pin only paths (`IDF_EXPORT`, `ESP_MATTER_PATH`) and check
  that the export scripts exist. CI never builds either port, so no toolchain is pinned
  there either. Treat the pair of installs as an external prerequisite you manage.
- Stage the install if disk is tight: plain ESP-IDF with the `esp32s3` target is enough
  for the bench reader app (a few GB). The matter-lock app additionally needs esp-matter,
  which is much larger because connectedhomeip is heavy.

## 4. Engine port surface (Phase 1, concrete)

The UWB engine (`modules/woz_uwb`) already compiles as pure C on host (`tests/host/sources.sh`,
13 core files). Minimal ESP-IDF port:

- OS seam: extend `modules/woz_uwb/src/facade/woz_alloc.h` (already wraps `k_malloc` and monotonic
  microseconds) into a `woz_os` seam with a FreeRTOS backend. Real call sites are few:
  `uwb_min.c` (`k_uptime_get` busy-wait deadlines ~188-307, `k_msleep` 31/122/362) and
  `fira_session.c` (`k_uptime_get` 71/125). `k_work`/`k_timer` uses are diagnostics/self-test/log
  only (`uwb_rxdiag.c`, `uwb_selftest.c`, `woz_logfmt.c`) and stub or defer.
- DW3000 platform shim, rewrite on ESP-IDF SPI-master + GPIO: `deps/dw3000/platform/dw3000_spi.c`
  (241), `dw3000_hw.c` (298), `deca_port.c` (60). Leave `deca_compat.c` (1352, vendor) logic intact.
- Link seam: the 5 `-Wl,--wrap=dwt_*` flags (`modules/woz_uwb/CMakeLists.txt`) via
  `target_link_options`. Only `--wrap=dwt_rxenable` is load-bearing (`ccc_shim_rx.c` programs the
  CCC key/IV on every RX-arm); the rest are diagnostics or have no live caller.
  `xtensa-esp32s3-elf-ld` is GNU binutils; verify `--wrap` once.
- Crypto seam: `ccc_crypto_mbedtls.c` (already selected by the existing scaffold's `prj.conf`).
- Placement: pin engine + DW3000 SPI to core 1, BLE/Wi-Fi on core 0. Hot buffers in internal SRAM,
  not PSRAM (jitter).

## 5. Phased plan, and how each phase went

| Phase | Plan | Outcome |
|---|---|---|
| 1 — engine on ESP-IDF | Compile the engine behind an OS seam, write a DW3000 backend, range against a second board with a canned URSK. | **Done.** The compat layer let `modules/woz_uwb/src` and `deps/dw3000` compile unchanged, and `--wrap` behaved as on any GNU ld. The one surprise was hardware, not software: an EVB power-select jumper hid the radio for days. |
| 2 — BLE transport | Reimplement GATT + L2CAP CoC on NimBLE. Estimated the dominant rewrite. | **Done, and easier than planned.** `ports/esp32/components/aliro_ble`. Advertising, the SPSM/version characteristics, and the CoC came up quickly. |
| 3 — reader logic and the ranging key | Port the reference's reader logic and storage; the derived key enters the engine at the existing seam. | **Done, and far larger than planned.** The reference does not derive the key in portable code at all — a closed ARM-only library does. This phase became a from-scratch reimplementation of the credential authentication, key schedule, secure channels, and wire codec, plus a provisioning seam. See [`porting-esp32-phase3.md`](porting-esp32-phase3.md). |
| 4 — Matter provisioning | esp-matter door lock over Wi-Fi with the Aliro cluster and delegate, so the lock self-commissions and provisions a key into the wallet. | **Done.** `ports/esp32/apps/matter-lock`. The Tier-A bet paid off: the cluster and delegate came across with modest change. The reader attaches to esp-matter's NimBLE host rather than starting its own. |
| 4.5 — real-time ranging | Not in the plan. | **Done, and unplanned.** Making the DS-TWR responder hold a 2 ms slot on ESP32 was its own campaign. Nothing here was a logic bug; the shared engine was already correct. |
| 5 — NFC tap | Integrate a first-party Aliro-over-NFC stack rather than reimplementing RFAL. | **Not attempted.** No NFC reader was sourced. The ESP32 target is approach-unlock only. |

## 6. Hardware bill of materials

- ESP32-S3-WROOM N16R8 dev board.
- DWM3000EVB (DW3110) for UWB on SPI2, eleven jumpers. Current pin map:
  [`ports/esp32/components/woz_uwb/port/board_pins.h`](../ports/esp32/components/woz_uwb/port/board_pins.h),
  wiring table in [`docs/esp32-bringup.md`](esp32-bringup.md).
- No NFC reader. Phase 5 was not attempted; if you pick this up, note that PN532-class
  parts are assumed too limited for Express / ECP timing and the reference uses ST25R.
- Matter over Wi-Fi on the S3 is not low-power like the nRF Thread sleepy end device.
  Fine for a mains-powered reader, wrong for a battery lock.

## 7. Risks and assumptions, resolved

- **Confirmed.** The Aliro Door Lock cluster is in connectedhomeip
  (`SetAliroReaderConfig` / `ClearAliroReaderConfig` and the feature flag), so esp-matter
  has it and the Tier-A carry-over worked as predicted.
- **Confirmed.** NimBLE's L2CAP CoC and GATT cover what the transaction needs, given a
  larger host task stack: the 4096-byte default overflows during software P-256.
- **Confirmed.** ESP-IDF's mbedTLS-PSA covers the ECDH, ECDSA, and AES-GCM the reader
  needs.
- **Confirmed.** `--wrap` on `xtensa-esp32s3-elf-ld` behaves as on any GNU ld;
  `verify_port.sh` guards the seam on every build.
- **Confirmed, with a caveat.** BLE and UWB coexist on one S3, but not for free: the
  ranging task is pinned to core 1 and the console to core 0, and the transaction runs
  synchronously on the BLE host task because driving it from elsewhere races the host.
- **Held.** The clean-room discipline was kept. Facts came from disassembling the vendor
  binary and from readable reference sources; no restricted source was copied.

## 8. If you are doing this yourself

Read [`docs/esp32-gotchas.md`](esp32-gotchas.md) first. It is the
document this plan should have been able to write in advance and could not: forty-odd
specific traps, each with what it looks like on a console and what actually fixed it.
Three cost more than a day each — an EVB power jumper, a ranging session id that is
derived rather than chosen, and a per-round log line that starved the DW3000 ISR task.
