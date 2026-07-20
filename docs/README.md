<!-- generated documentation — edit the source, not this file -->
# openaliro

**97 subsystems in 22 directories · 727/757 symbols documented (96%)**

**Start here:** [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_session.c`](architecture/modules.woz_uwb.src.aliro/aliro_uwb_session.c.md), [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md) — the doors into the codebase (nothing else imports them).

```mermaid
flowchart LR
  modules.woz_aliro.src --> modules.woz_aliro.include
  modules.woz_aliro.src --> modules.woz_uwb.src.aliro.include.aliro_uwb_adapter
  modules.woz_aliro.src --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_aliro.src --> modules.woz_uwb.src.facade
  modules.woz_aliro.src --> ports.esp32-idf.components.woz_uwb.compat.zephyr
  modules.woz_aliro.src --> ports.esp32-idf.components.woz_uwb.compat.zephyr.logging
  modules.woz_aliro_ecp.src --> ports.esp32-idf.components.woz_uwb.compat.zephyr.logging
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.aliro.include.aliro_uwb_adapter
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.aliro --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.aliro --> ports.esp32-idf.components.woz_uwb.compat.zephyr.logging
  modules.woz_uwb.src.aliro --> ports.esp32-idf.components.woz_uwb.compat.zephyr.sys
  modules.woz_uwb.src.aliro.include.aliro_uwb_adapter --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.aliro.include.cherry
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.driver
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.ccc --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.ccc --> ports.esp32-idf.components.woz_uwb.compat.zephyr
  modules.woz_uwb.src.ccc --> ports.esp32-idf.components.woz_uwb.compat.zephyr.logging
  modules.woz_uwb.src.ccc --> ports.esp32-idf.components.woz_uwb.compat.zephyr.sys
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.facade
  modules.woz_uwb.src.driver --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.driver --> ports.esp32-idf.components.woz_uwb.compat.zephyr
  modules.woz_uwb.src.driver --> ports.esp32-idf.components.woz_uwb.compat.zephyr.logging
  modules.woz_uwb.src.facade --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.facade --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.facade --> ports.esp32-idf.components.woz_uwb.compat.zephyr
  modules.woz_uwb.src.facade --> ports.esp32-idf.components.woz_uwb.compat.zephyr.logging
  modules.woz_uwb.src.facade --> ports.esp32-idf.components.woz_uwb.compat.zephyr.sys
  modules.woz_uwb.src.fira --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.fira --> ports.esp32-idf.components.woz_uwb.compat.zephyr
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.ccc
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.driver
  modules.woz_uwb.src.shell --> modules.woz_uwb.src.fira
  modules.woz_uwb.src.shell --> ports.esp32-idf.components.woz_uwb.compat.zephyr
  ports.esp32-matter.main --> ports.esp32-matter.main.lock
  ports.esp32s3.sample.src --> ports.esp32-idf.components.woz_uwb.compat.zephyr
  ports.esp32s3.sample.src --> ports.esp32-idf.components.woz_uwb.compat.zephyr.logging
```

## Directories

| directory | subsystems | documented |
|---|---|---|
| [`./`](architecture/root/README.md) | 3 | 14/14 (100%) |
| [`modules/woz_aliro/include/`](architecture/modules.woz_aliro.include/README.md) | 5 | 9/9 (100%) |
| [`modules/woz_aliro/src/`](architecture/modules.woz_aliro.src/README.md) | 10 | 116/123 (94%) |
| [`modules/woz_aliro_ecp/src/`](architecture/modules.woz_aliro_ecp.src/README.md) | 1 | 5/5 (100%) |
| [`modules/woz_uwb/src/aliro/`](architecture/modules.woz_uwb.src.aliro/README.md) | 10 | 104/118 (88%) |
| [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/`](architecture/modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/README.md) | 2 | 17/17 (100%) |
| [`modules/woz_uwb/src/aliro/include/cherry/`](architecture/modules.woz_uwb.src.aliro.include.cherry/README.md) | 4 | 36/36 (100%) |
| [`modules/woz_uwb/src/ccc/`](architecture/modules.woz_uwb.src.ccc/README.md) | 17 | 137/142 (96%) |
| [`modules/woz_uwb/src/driver/`](architecture/modules.woz_uwb.src.driver/README.md) | 7 | 43/43 (100%) |
| [`modules/woz_uwb/src/facade/`](architecture/modules.woz_uwb.src.facade/README.md) | 7 | 25/25 (100%) |
| [`modules/woz_uwb/src/fira/`](architecture/modules.woz_uwb.src.fira/README.md) | 3 | 10/10 (100%) |
| [`modules/woz_uwb/src/shell/`](architecture/modules.woz_uwb.src.shell/README.md) | 1 | 12/12 (100%) |
| [`ports/esp32-idf/components/aliro_ble/`](architecture/ports.esp32-idf.components.aliro_ble/README.md) | 1 | 42/42 (100%) |
| [`ports/esp32-idf/components/aliro_reader/`](architecture/ports.esp32-idf.components.aliro_reader/README.md) | 1 | 5/5 (100%) |
| [`ports/esp32-idf/components/woz_uwb/compat/zephyr/`](architecture/ports.esp32-idf.components.woz_uwb.compat.zephyr/README.md) | 1 | 13/15 (86%) |
| [`ports/esp32-idf/components/woz_uwb/compat/zephyr/logging/`](architecture/ports.esp32-idf.components.woz_uwb.compat.zephyr.logging/README.md) | 1 | 0/0 (0%) |
| [`ports/esp32-idf/components/woz_uwb/compat/zephyr/sys/`](architecture/ports.esp32-idf.components.woz_uwb.compat.zephyr.sys/README.md) | 3 | 8/8 (100%) |
| [`ports/esp32-idf/components/woz_uwb/port/`](architecture/ports.esp32-idf.components.woz_uwb.port/README.md) | 4 | 30/30 (100%) |
| [`ports/esp32-idf/main/`](architecture/ports.esp32-idf.main/README.md) | 3 | 16/16 (100%) |
| [`ports/esp32-matter/main/`](architecture/ports.esp32-matter.main/README.md) | 7 | 26/26 (100%) |
| [`ports/esp32-matter/main/lock/`](architecture/ports.esp32-matter.main.lock/README.md) | 5 | 57/59 (96%) |
| [`ports/esp32s3/sample/src/`](architecture/ports.esp32s3.sample.src/README.md) | 1 | 2/2 (100%) |

## Hotspots

*Mined from git history as of `6558e2d`.*

**Most-changed:** [`modules/woz_uwb/src/ccc/ccc_shim_rx.c`](architecture/modules.woz_uwb.src.ccc/ccc_shim_rx.c.md) (11 commits), [`ports/esp32-matter/main/app_main.cpp`](architecture/ports.esp32-matter.main/app_main.cpp.md) (8 commits), [`build.sh`](architecture/root/build.sh.md) (7 commits), [`ports/esp32-idf/components/aliro_ble/aliro_ble.c`](architecture/ports.esp32-idf.components.aliro_ble/aliro_ble.c.md) (7 commits), [`bootstrap.sh`](architecture/root/bootstrap.sh.md) (4 commits).
