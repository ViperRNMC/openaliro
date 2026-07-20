<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/woz_uwb_facade.h`

Public header for UWB facade: exposes Aliro DS-TWR responder lifecycle and range query; the CCC
engine is bound and unbound via internal ursk and stop calls.

**used by** [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](../modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md), [`modules/woz_uwb/src/driver/uwb_selftest.c`](../modules.woz_uwb.src.driver/uwb_selftest.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](woz_uwb_facade.c.md)  ·  **discussed in** [`docs/porting-esp32-phase3.md`](../../porting-esp32-phase3.md)

## API

### `int woz_uwb_start_aliro(const struct woz_uwb_aliro_cfg *cfg)`
`modules/woz_uwb/src/facade/woz_uwb_facade.h:37`

Start the CCC DS-TWR responder bound to a live Aliro credential; returns 0 on success.
