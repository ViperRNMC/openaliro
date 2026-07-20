<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h`

@file aliro_uwb_adapter.h — reader-device public interface.

**depends on** [`modules/woz_uwb/src/aliro/include/cherry/cherry.h`](../modules.woz_uwb.src.aliro.include.cherry/cherry.h.md), [`modules/woz_uwb/src/aliro/include/cherry/cherry_ccc.h`](../modules.woz_uwb.src.aliro.include.cherry/cherry_ccc.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_adapter.c`](../modules.woz_uwb.src.aliro/aliro_uwb_adapter.c.md), [`modules/woz_uwb/src/aliro/aliro_uwb_internal.h`](../modules.woz_uwb.src.aliro/aliro_uwb_internal.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.c`](../modules.woz_uwb.src.aliro/aliro_uwb_msg.c.md)

## API

### `struct aliro_uwb_preferred_hopping_configs`
`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h:46`

@brief Ordered hopping preferences (at least one default sequence required).

#### `struct aliro_uwb_preferred_hopping_configs preferred_hopping_configs`
`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h:58`

@brief Ordered preferred hopping configurations.

### `struct cherry *cherry_ctx,`
`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h:76`

Opaque CCC context handle, threaded through to the CCC session API calls.

### `struct cherry_core_event_device_capabilities *caps,`
`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h:82`

@brief Device capabilities (channels, PRF, supported algorithms) advertised by
the reader during CCC discovery.
@param caps Device capabilities to advertise during CCC discovery.

### `struct aliro_uwb_adapter_reader_config *config);`
`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h:87`

@brief Reader-side selection preferences (borrowed for the adapter's lifetime).
@param config Reader adapter configuration borrowed for the adapter's lifetime.

### `struct cherry_common_diag_cfg config);`
`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h:97`

@brief Diagnostic configuration for CCC reporting (ranging, signal metrics, session
status).
@param config Diagnostic configuration to apply for CCC reporting.

### `void aliro_uwb_adapter_destroy(struct aliro_uwb_adapter *aliro_ctx)`
`modules/woz_uwb/src/aliro/include/aliro_uwb_adapter/aliro_uwb_adapter.h:103`

@brief Release an adapter context.
@param aliro_ctx Adapter context to release.
