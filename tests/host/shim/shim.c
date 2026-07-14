/* Host shim definitions — the few non-inline stubs the pure-logic modules need
 * at link time. Header-only stubs live in tests/host/shim/**; this file holds
 * the DW3000 STS register no-ops, which capture their arguments so a unit test
 * can verify ccc_sts.c packed the key/IV correctly. */
#include <stddef.h>

#include "deca_device_api.h"

dwt_sts_cp_key_t woz_host_last_sts_key;
dwt_sts_cp_iv_t woz_host_last_sts_iv;
unsigned int woz_host_sts_loadiv_calls;

void dwt_configurestskey(dwt_sts_cp_key_t *key)
{
	if (key != NULL) {
		woz_host_last_sts_key = *key;
	}
}

void dwt_configurestsiv(dwt_sts_cp_iv_t *iv)
{
	if (iv != NULL) {
		woz_host_last_sts_iv = *iv;
	}
}

void dwt_configurestsloadiv(void)
{
	woz_host_sts_loadiv_calls++;
}
