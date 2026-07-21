<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/ccc/ccc_shim_wrap.c`

@file ccc_shim_wrap.c — per-frame STS interception (ld --wrap=dwt_configurestsiv) substituting
CCC STS for the FiRa MAC; target only.

**depends on** [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/ccc/ccc_shim.h`](ccc_shim.h.md), [`modules/woz_uwb/src/facade/woz_bytes.h`](../modules.woz_uwb.src.facade/woz_bytes.h.md)

## API

### `void ccc_shim_wrap_log_reset(void)`
`modules/woz_uwb/src/ccc/ccc_shim_wrap.c:43`

@brief Reset the frame logging counter to zero, used for re-enabling diagnostics after the first
N frames.

### `static void pack_key(dwt_sts_cp_key_t *out, const uint8_t dursk[CCC_DURSK_LEN])`
`modules/woz_uwb/src/ccc/ccc_shim_wrap.c:49`

@brief Pack a 16-byte `dURSK` into the DW3000 STS-key register image.

**called by** `__wrap_dwt_configurestsiv`

### `static void pack_iv(dwt_sts_cp_iv_t *out, const uint8_t sts_v[CCC_STS_V_LEN])`
`modules/woz_uwb/src/ccc/ccc_shim_wrap.c:68`

@brief Pack a 16-byte STS-V into the DW3000 STS-IV register image (whole-16 reverse then per-word
LE).
@param out DW3000 STS-IV register structure (iv0, iv1, iv2, iv3).
@param sts_v 16-byte STS-V value.

**called by** `__wrap_dwt_configurestsiv`

### `void __wrap_dwt_configurestsiv(dwt_sts_cp_iv_t *pStsIv)`
`modules/woz_uwb/src/ccc/ccc_shim_wrap.c:86`

@brief Intercept DW3000 STS IV configuration, deriving dURSK and STS-V from CCC secrets and
configuring the radio; falls through to real dwt_configurestsiv if shim is inactive.
@param pStsIv DW3000 STS-IV register structure (also holds the FiRa blob index).

**calls** `pack_iv`, `pack_key`
