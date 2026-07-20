<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/include/cherry/cherry.h`

@file cherry.h — Cherry core (context + device-capabilities) interface.

**depends on** [`modules/woz_uwb/src/aliro/include/cherry/cherry_common.h`](cherry_common.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](../modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_adapter.h.md), [`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_session.h`](../modules.woz_uwb.src.aliro.include.aliro_uwb_adapter/aliro_uwb_session.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](cherry_ccc.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_session.h`](cherry_session.h.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](../modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md)

## API

### `struct cherry_fira_capabilities`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:36`

@brief Opaque per-technology FiRa capability blob; unused on this lock since only CCC
capabilities are populated.

### `struct cherry_ccc_capabilities`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:40`

@brief Opaque CCC device capabilities structure, not accessible outside the cherry library.

### `struct cherry_radar_capabilities`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:44`

@brief Opaque radar device capabilities structure, not accessible outside the cherry library.

### `struct cherry_core_event_device_capabilities`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:50`

@brief UWBS capability container reported by the peer during device discovery; only the CCC
capabilities member is consulted.

#### `struct cherry_fira_capabilities *fira_capabilities`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:56`

@brief FiRa capabilities advertised by the peer during device discovery; unused on this
lock.

#### `struct cherry_ccc_capabilities *ccc_capabilities`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:60`

@brief CCC capabilities advertised by the peer during device discovery.

#### `struct cherry_radar_capabilities *radar_capabilities`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:64`

@brief Radar capabilities advertised by the peer during device discovery.

### `typedef void (*cherry_core_cb_t)(struct cherry_core_event *event, void *user_data)`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:70`

@brief Callback type for core (non-session) Cherry notification events.

### `void cherry_destroy_sync(struct cherry *ctx)`
`modules/woz_uwb/src/aliro/include/cherry/cherry.h:79`

@brief Synchronously release a Cherry context and its resources.
@param ctx Cherry context to destroy.
