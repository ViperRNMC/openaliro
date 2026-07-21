# Porting to a new chipset

How the UWB engine moves to a new chipset, what it costs, and how to prove a port did not
change the code the validated target runs.

The primary target is the nRF5340 DK on NCS v3.3.0 (hardware-validated end to end). A
second port, ESP32-S3 on ESP-IDF, lives in [`ports/esp32/`](../ports/esp32/).

## 1. The contract

Everything the ranging engine needs from a platform is the two headers in
[`modules/woz_port/include/`](../modules/woz_port/include/):

- **[`woz_port.h`](../modules/woz_port/include/woz_port.h)** — eight functions plus one mutex:

  | Function | Meaning |
  |---|---|
  | `woz_malloc` / `woz_calloc` / `woz_free` | heap |
  | `woz_uptime_us` | monotonic microseconds since boot |
  | `woz_uptime_ms` | monotonic milliseconds since boot |
  | `woz_sleep_ms` | relinquish the CPU for at least N ms |
  | `woz_sleep_us` | short busy-wait, microseconds (`deca_sleep`) |
  | `woz_cycle_get_32` | free-running counter, RX-arm latency probe |
  | `woz_mutex_init/lock/unlock` | blocking mutex (Aliro reader trust store; three lines per backend) |

- **[`woz_log.h`](../modules/woz_port/include/woz_log.h)** — `LOG_ERR/WRN/INF/DBG`, the
  hexdump variants, `LOG_MODULE_REGISTER/DECLARE`, and `woz_printf`.

Both select a backend from `__ZEPHYR__`, `ESP_PLATFORM`, or `WOZ_PORT_HOST`, and `#error`
if none is defined rather than guessing. Two further headers in
`modules/woz_uwb/src/facade/`, `woz_bytes.h` (endian-neutral load/store) and `woz_util.h`
(`MIN`/`MAX`/`ARRAY_SIZE`/`IS_ENABLED`), are pure code with no platform content at all;
they are shared by every target including Zephyr.

**Deliberately not in the contract:** work queues, timers, and init hooks. Those appear only
in `uwb_rxdiag.c`, `uwb_selftest.c`, `woz_logfmt.c`, `woz_logquiet.c` and `aliro_shell.c`,
which are Zephyr-only by design and are in no port's source list. The `k_work` / `k_sem` /
`k_poll` surface used by `dw3000_spi.c` and `dw3000_hw.c` is likewise excluded, because every
port supplies its own backend for those two files. Adding any of it would multiply the port
surface for code that never runs on the ranging path.

## 2. What a port costs

The cost axis is the **RTOS**, not the chipset. Under Zephyr the SPI and GPIO layers are
devicetree-abstracted, so a new Zephyr-supported SoC needs no C at all.

| Tier | Work | Targets | Effort |
|---|---|---|---|
| **0. Board file** | Devicetree overlay, roughly 50 lines. No C. | nRF52840, nRF54L15, EFR32MG24, STM32WB55, any Zephyr SoC with SPI + GPIO IRQ | Hours |
| **1. New RTOS** | A branch in `woz_port.h` + `woz_log.h` (about 55 lines), plus a DW3000 SPI/GPIO/IRQ backend (about 350 lines) | ESP-IDF (**done**), Pico SDK (RP2350), STM32Cube + FreeRTOS, bare metal | 2 to 4 days |
| **2. New UWB silicon** | A new driver under the `woz_uwb_facade.h` seam. Not a port. | Anything that is not DW3xxx | Weeks, gated on driver availability |

Effort figures other than ESP-IDF are estimates from the measured line counts, not from
completed ports.

**Scope of the Tier 0 claim.** It applies to the **UWB engine module**, which is
SoC-neutral apart from one guarded block: the nRF5340 HFCLK boost in `woz_uwb_facade.c`.
It does **not** cover the full Matter door-lock product. `build.sh` pins
`nrf5340dk/nrf5340/cpuapp` and drives a sysbuild with a separate `ipc_radio` network-core
image, so moving the whole application to a single-core part such as nRF52840 is a
sysbuild and Matter-transport exercise well beyond a devicetree overlay. Porting the
engine to a new Zephyr board is cheap; porting the product is not, and the two should not
be quoted at the same price.

For reference, the ESP-IDF port's entire target-specific surface for the ranging engine
is `ports/esp32/components/woz_uwb/port/`: `dw3000_spi.c` (169), `dw3000_hw.c` (180) and
`woz_wrap_stubs.c` (21). It carries no Zephyr compatibility layer.

**Beyond the engine: the full reader.** The tiers above cover secure ranging. A complete
lock additionally needs the credential-auth reader from `modules/woz_aliro`, which brings
two more per-platform seams, both small and both with ESP-IDF worked examples:

- a **BLE transport** implementing [`aliro_ble.h`](../modules/woz_aliro/include/aliro_ble.h)
  (the NimBLE backend is `ports/esp32/components/aliro_ble/aliro_ble.c`);
- a **storage backend** for the `aliro_prov` trust store (the NVS one is
  `ports/esp32/components/aliro_reader/aliro_prov_nvs.c`).

The crypto layer itself (`aliro_hash.c`, `aliro_crypto.c`, `aliro_prim_psa.c`) is shared
source over PSA/mbedTLS and ports without platform code.

## 3. Build tiers

- **Full Aliro UWB** — `CONFIG_WOZ_UWB=y`, `WOZ_UWB_RESPONDER=y`, `WOZ_ALIRO=y`.
- **Bring-up only** — `CONFIG_WOZ_UWB=y` alone compiles just `uwb_min.c`.
- **No UWB (NFC-only)** — `CONFIG_WOZ_UWB=n`. The whole module is wrapped in
  `if(CONFIG_WOZ_UWB)` and contributes nothing; every external call site in
  `ports/nrf5340dk/patches/custom_impl-uwb.patch` is `#ifdef CONFIG_WOZ_ALIRO`, so the build
  links clean with no UWB silicon present.

  This matters more than it looks. Aliro makes NFC the mandatory transport and BLE and UWB
  optional, so an NFC-only lock is a legitimate certified device, and this tier removes the
  DWM3000EVB from the bill of materials. Note that it also uses none of the ranging engine:
  the NFC path needs the credential-auth layer, not `modules/woz_uwb`.

### Crypto seam

`ccc_kdf.h` needs one AES-ECB primitive, supplied by exactly one of:

- `ccc_crypto_psa.c` — `CONFIG_WOZ_CRYPTO_PSA` (default on nRF, backed by nrf_security).
- `ccc_crypto_mbedtls.c` — `CONFIG_WOZ_CRYPTO_MBEDTLS`, for SoCs without a PSA provider.
  This is what the ESP32-S3 port uses.

Both honour the same contract; pick whichever the target's crypto stack provides.

### SoC-specific seams

`woz_uwb_facade.c` boosts the nRF5340 app-core HFCLK to 128 MHz for the DW3000 SPI bus,
guarded by `CONFIG_SOC_NRF5340_CPUAPP`. Other SoCs clock their SPI controller independently,
so it compiles to a no-op. A new target needs an equivalent only if its SPI clock is
divided at boot.

### Linker seam

The CCC STS substitution rides one load-bearing link-time intercept,
`-Wl,--wrap=dwt_rxenable`, where `ccc_shim_rx.c` programs the CCC key and IV on every
RX-arm; the other `--wrap=dwt_*` flags in the build files are diagnostics or have no
live caller. Any GNU ld supports `--wrap`; it is confirmed working on both
`arm-zephyr-eabi-ld` and `xtensa-esp32s3-elf-ld`. A port to a non-GNU linker would
need a different interception strategy.

## 4. Verifying a port did not change the validated target

Porting work touches shared code, so the nRF5340 image must be shown to be unaffected
rather than assumed to be. The check that catches real regressions is per-object, because
whole-image numbers are dominated by the Matter application and will hide a small engine
change.

```sh
SIZE=<zephyr-sdk>/arm-zephyr-eabi/bin/arm-zephyr-eabi-size
D=build/matter-aliro-door-lock-app/modules/woz_uwb/CMakeFiles/woz_uwb.dir/src

make build                                        # before the change
find $D -name '*.obj' | sort | xargs $SIZE > /tmp/before.txt
# ... make the change ...
make build
find $D -name '*.obj' | sort | xargs $SIZE > /tmp/after.txt
diff /tmp/before.txt /tmp/after.txt                # must be empty for a pure refactor
```

Repeat for the vendored DW3000 objects under
`build/matter-aliro-door-lock-app/modules/dw3000/` if `deps/dw3000` was touched, and run
`make test` (the host KAT suite, no toolchain or hardware needed) plus `make test-port`.

A byte-identical size table proves codegen is unchanged; it does not prove the port works.
Only a bench run against a phone does that.

## 5. Vendored DW3000 and the `printk` alias

`deps/dw3000` is vendor source with one local addition: DIAG tracing this project added to
`deca_compat.c`, `deca_interface.c` and `dw3000_device.c` (about 18 `printk` call sites,
gated on `CONFIG_WOZ_PRETTY_SHELL`). To keep those files to a one-line include change rather
than rewriting vendor call sites, `woz_log.h` aliases `printk` itself on non-Zephyr targets.
That alias exists only for this purpose and should be removed with the DIAG tracing.

`deca_port.c` is shared by all ports and uses `woz_sleep_ms` / `woz_sleep_us`;
`dw3000_spi.c`, `dw3000_hw.c` and `dw3000_spi_trace.c` are Zephyr-specific and each port
replaces them.

## 6. Known-blocked targets

Blocked on driver availability, not on engineering:

- **NXP SR150** — NDA/production access only.
- **QM33 / QM35 as UWB silicon** — the public `qm35-sdk` on GitLab is clonable without an
  NDA, but it ships FiRa ranging and 360° AoA and does not mention Aliro. The Nordic and
  Qorvo Aliro reference application is distributed to "early technology adopters" on
  request; the terms are not public. Whether the public SDK exposes the STS key injection
  this engine needs is unverified. Its licence agreement was not read and may restrict
  redistribution.
