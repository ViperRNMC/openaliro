<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/fira/fira_session.c`

@file fira_session.c — Range + URSK store for the CCC Pre-POLL responder.

**depends on** [`modules/woz_port/include/woz_port.h`](../modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/ccc/aliro_kdf.h`](../modules.woz_uwb.src.ccc/aliro_kdf.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](fira_session.h.md)  ·  **discussed in** [`docs/porting-esp32.md`](../../porting-esp32.md)

## API

### `void fira_session_set_provisioned_ursk(const uint8_t *ursk)`
`modules/woz_uwb/src/fira/fira_session.c:18`

@brief Stash an Aliro URSK for the CCC Pre-POLL STS decode; NULL clears it.

### `const uint8_t *fira_session_get_ursk(void)`
`modules/woz_uwb/src/fira/fira_session.c:29`

@brief The stashed Aliro URSK (32 bytes), or NULL if none — for the Pre-POLL decode.

### `uint32_t fira_session_current_slot(void)`
`modules/woz_uwb/src/fira/fira_session.c:34`

@brief STS-index slot clock (inert without a MAC time base); returns 0.

### `bool fira_session_last_range(int32_t *cm_out, uint16_t *addr_out, uint8_t *nlos_out, uint32_t *block_out, int64_t *age_ms_out)`
`modules/woz_uwb/src/fira/fira_session.c:52`

@brief Fetch the most recent valid DS-TWR range; out-params optional (NULL to skip).

### `bool fira_session_range_plausible(int32_t cm)`
`modules/woz_uwb/src/fira/fira_session.c:77`

@brief Layer 1: true if @p cm is a physically plausible DS-TWR distance.

**called by** `fira_session_set_ccc_range_cm`

### `bool fira_session_sts_quality_ok(int32_t driver_verdict, int16_t quality_index)`
`modules/woz_uwb/src/fira/fira_session.c:82`

@brief Layer 2: true if the STS correlated well enough to trust its timestamp.
@param driver_verdict  dwt_readstsquality() return (>=0 good, <0 bad).
@param quality_index   the signed STS quality index it wrote.

### `bool fira_session_range_trusted(void)`
`modules/woz_uwb/src/fira/fira_session.c:87`

@brief Layer 4: true once >= K consecutive plausible, mutually consistent
ranges have been latched. Cleared by any implausible or outlier block.

### `void fira_session_set_ccc_range_cm(int32_t cm, uint32_t block)`
`modules/woz_uwb/src/fira/fira_session.c:92`

@brief Latch a CCC DS-TWR range so it flows up the Aliro mRangingData seam.

**calls** `fira_session_range_plausible`
