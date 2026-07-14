/* Host shim for <deca_device_api.h> — only the STS key/IV surface ccc_sts.c
 * needs. The register writes are no-ops here, but the values are captured into
 * woz_host_last_sts_* so a unit test can assert the derivation packed them
 * correctly (field layout matches the DW3000 driver's cp_key/cp_iv structs). */
#ifndef WOZ_HOST_SHIM_DECA_DEVICE_API_H
#define WOZ_HOST_SHIM_DECA_DEVICE_API_H

#include <stdint.h>

typedef struct {
	uint32_t key0;
	uint32_t key1;
	uint32_t key2;
	uint32_t key3;
} dwt_sts_cp_key_t;

typedef struct {
	uint32_t iv0;
	uint32_t iv1;
	uint32_t iv2;
	uint32_t iv3;
} dwt_sts_cp_iv_t;

/* Last values handed to the (no-op) register writes — for test assertions. */
extern dwt_sts_cp_key_t woz_host_last_sts_key;
extern dwt_sts_cp_iv_t woz_host_last_sts_iv;
extern unsigned int woz_host_sts_loadiv_calls;

void dwt_configurestskey(dwt_sts_cp_key_t *key);
void dwt_configurestsiv(dwt_sts_cp_iv_t *iv);
void dwt_configurestsloadiv(void);

#endif /* WOZ_HOST_SHIM_DECA_DEVICE_API_H */
