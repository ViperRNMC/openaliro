# Memory usage

Scope: the primary nRF5340 DK image only. The ESP32-S3 apps have their own budgets and
partition layout (`ports/esp32/apps/*/partitions.csv`) and are not measured here.

Snapshot measured 2026-07-19 from the build artifacts of 2026-07-17 19:55 in `build/`
(repo HEAD `56df8df` at measurement time). `ports/nrf5340dk/overlays/woz-aliro.conf` and several
`modules/woz_uwb` sources are newer than these artifacts, so expect small shifts on the next
`make build`.
## Hardware budgets

nRF5340 (app core + net core) plus the DK's on-board MX25R64 QSPI NOR.

| Resource | Physical | Usable by image | Reserved, and why |
|---|---:|---:|---|
| App-core flash | 1024 KiB | 988 KiB | 4 KiB `factory_data` + 32 KiB `settings_storage`; no bootloader (MCUboot dropped) |
| App-core RAM (`sram0`) | 512 KiB | 448 KiB | top 64 KiB (`sram0_shared` at `0x20070000`) is the app/net IPC (RPMsg) region |
| Net-core flash | 256 KiB | 256 KiB | single `ipc_radio` image |
| Net-core RAM (`sram1`) | 64 KiB | 64 KiB | the IPC buffer lives in app-core RAM, not here |
| External QSPI (MX25R64) | 8 MiB | 8 MiB | 128 KiB `external_nvs` active; 7.875 MiB `external_flash` has no consumer |

The IPC region is carved from app-core RAM because that is the only RAM both cores can
address: the net-core devicetree maps both `memory@20000000` and `memory@21000000`, while the
app-core devicetree maps only its own `memory@20000000`. Both images include
`nrf5340_shared_sram_partition.dtsi` and agree on the same 64 KiB window.

## Usage summary

| Image / region | Budget | Used | Free | Used |
|---|---:|---:|---:|---:|
| App-core flash (`app` partition) | 988 KiB | 877.1 KiB (898,108 B) | 110.9 KiB | 88.8% |
| App-core RAM | 448 KiB | 335.8 KiB (343,836 B) | 112.2 KiB | 75.0% |
| Net-core flash (`ipc_radio`) | 256 KiB | 171.2 KiB (175,280 B) | 84.8 KiB | 66.9% |
| Net-core RAM | 64 KiB | 56.1 KiB (57,468 B) | 7.9 KiB | 87.7% |

Tightest budget: net-core RAM, 7.9 KiB free. That is where growth hits first.

RAM composition (KiB):

| Image | .data | .bss | .noinit | small sections |
|---|---:|---:|---:|---:|
| app core | 20.3 | 159.1 | 154.8 | 1.6 |
| net core | 1.1 | 29.9 | 25.1 | 0.0 |

## Flash maps

Internal flash, app core (`ports/nrf5340dk/overlays/pm_static.yml`):

| Partition | Address | Size | Purpose |
|---|---|---:|---|
| `app` | `0x00000` | 988 KiB | application image, boots from 0x0, no bootloader |
| `factory_data` | `0xf7000` | 4 KiB | Matter factory data |
| `settings_storage` | `0xf8000` | 32 KiB | NVS settings (8 sectors x 4 KiB); pinned so commissioning survives reflash |

Internal flash, net core: `ipc_radio` image at `0x1000000`, owns all 256 KiB.

External QSPI (MX25R64, 8 MiB):

| Partition | Address | Size | Purpose |
|---|---|---:|---|
| `external_nvs` | `0x00000` | 128 KiB | Aliro reader storage, AEAD-encrypted with rollback protection (`CONFIG_DOOR_LOCK_EXTERNAL_NVS`) |
| `external_flash` | `0x20000` | 7.875 MiB | allocated, no consumer (no MCUboot, no XIP, no DFU bank) |

## Largest RAM objects, app core

From the ELF symbol table (accounts for 343,770 of 343,836 B).

| KiB | Object | Sized by / defined in |
|---:|---|---|
| 96.0 | `kheap__system_heap` (kernel `k_malloc` pool) | `CONFIG_HEAP_MEM_POOL_SIZE=98304` |
| 19.0 | `chip::System::PacketBuffer` pool | `CONFIG_CHIP_SYSTEM_PACKETBUFFER_POOL_SIZE=15` (15 x 1,300 B) |
| 16.0 | CHIP dedicated heap arena (`sHeapMemory`) | `CONFIG_CHIP_MALLOC_SYS_HEAP_SIZE=16384`, `SysHeapMalloc.cpp` |
| 16.0 | deferred-logging ring (`buf32`) | `CONFIG_LOG_BUFFER_SIZE=16384`, `log_core.c` |
| 13.6 | `chip::Server` static | CHIP |
| 12.1 | OpenThread instance (`ot::gInstanceRaw`) | OpenThread MTD |
| 12.0 | Matter access manager static (`AccessManager<0x1C2>`; the template parameter is the supported-credential-types bit mask, not a slot count) | add-on `matter_access` |
| 9.9 | `chip::app::InteractionModelEngine` | CHIP |
| 8.1 | CHIP thread stack | CHIP platform |
| 8.0 | `mbedtls_heap` | `CONFIG_MBEDTLS_HEAP_SIZE=8192` |
| 6.1 | `z_main_stack` | `CONFIG_MAIN_STACK_SIZE=6144` |
| 5.8 | PSA crypto key-slot table (`global_data`) | `psa_crypto_slot_management.c` |
| 5.3 | 802.15.4 spinel serialization IPC ring (`ring_buffer`) | `nrf_802154_spinel_backend_ipc.c` |
| 5.1 | Aliro work-queue stack | add-on |
| rest | ~770 smaller objects (thread stacks, BT pools, drivers) | |

Heaps total 120 KiB, 26.8% of app RAM: 96 kernel + 16 CHIP + 8 mbedTLS. Matter/CHIP
allocations bypass the kernel pool (`CONFIG_CHIP_MALLOC_SYS_HEAP_OVERRIDE=y`).
`CONFIG_SYS_HEAP_RUNTIME_STATS=y` is already compiled in, but no shell command exposes it
(`CONFIG_KERNEL_SHELL` unset), so there is no live readout today.

The UWB engine is not a RAM factor: `woz_uwb` + `deps/dw3000` together hold about 3.8 KiB.

## Largest RAM objects, net core

| KiB | Object | Sized by / defined in |
|---:|---|---|
| 11.8 | `sdc_mempool` (SoftDevice Controller pool) | `hci_driver.c`; scales with `CONFIG_BT_MAX_CONN=5` and BT buffer counts |
| 8.0 | `kheap__system_heap` | `CONFIG_HEAP_MEM_POOL_SIZE=8192` |
| 5.3 | 802.15.4 spinel serialization IPC ring (`ring_buffer`) | `nrf_802154_spinel_backend_ipc.c` |
| 2.7 | HCI RX `net_buf` pool | host-controller IPC |
| 2.0 x3 | sys work queue, main, interrupt stacks | defaults |
| 1.0-1.5 | 802.15.4 RX buffers, HCI/ACL pools, remaining stacks | `CONFIG_NRF_802154_RX_BUFFERS=8` (already trimmed) |

## Size-relevant configuration as built

- `CONFIG_SIZE_OPTIMIZATIONS=y` (-Os). `CONFIG_SIZE_OPTIMIZATIONS_AGGRESSIVE` (-Oz) exists
  and is off. `LTO` exists in this Zephyr and is off.
- `CONFIG_DOOR_LOCK_RELEASE` unset: links the debug variant of the closed Nordic Aliro
  library (`lib/aliro/bin/debug/cortex-m33/libaliro_ble.a`, ~566 KB archive vs ~449 KB
  release; archive size, not linked size).
- App shell on (`CONFIG_SHELL=y`, serial). App logging on, deferred, default level 0,
  16 KiB buffer. Net-core logging fully off.
- `CONFIG_ASSERT=y` on both cores (app strips condition/message strings, net is verbose).
- BLE: `CONFIG_BT_MAX_CONN=5` on both cores; ACL TX/RX 271 B; EVT RX count 10.
- Net core already hand-trimmed by `ports/nrf5340dk/overlays/ipc_radio.conf` (verified applied
  in the generated `.config`): Coded PHY off, 802.15.4 RX buffers 20 to 8, pending-child
  tables 1+1 (MTD-SED). The usage numbers above are after this trim.

## Method

- Used/free comes from the PT_LOAD program headers of both `zephyr.elf` images, measured
  against the partition and devicetree budgets above.
- Cross-checked two independent ways: flash against a data-byte count of `merged.hex` /
  `merged_CPUNET.hex` (agreement within 8 B and 14 B); RAM against the sum of allocated ELF
  sections (within 53 B and 5 B).
- Per-object RAM attribution from the ELF symbol table; file provenance for static objects
  from `STT_FILE` markers.
- Per-archive flash attribution from the linker map was attempted and did not validate
  against the totals; it is deliberately omitted. Treat any per-library flash-size claim as
  unmeasured.

To refresh after a rebuild: re-derive from `build/matter-aliro-door-lock-app/zephyr/zephyr.elf`,
`build/ipc_radio/zephyr/zephyr.elf`, and `build/partitions.yml`.
