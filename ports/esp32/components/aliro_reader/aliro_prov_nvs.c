// NVS-backed persistence for Aliro reader provisioning: loads and stores the serialized reader
// identity and trust store built by aliro_prov.c.
// Lazily initializes NVS on first use; safe to call alongside aliro_ble's own nvs_flash_init.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_prov (NVS backend) — target-only load/store of the provisioning blob.
 * The portable serialisation + dev fallback + trust logic is in aliro_prov.c
 * (host-KAT'd); this file only moves that blob in and out of NVS. Not compiled
 * on host (kept out of the test build).
 */
#include <string.h>

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include "aliro_prov.h"

static const char *TAG = "aliro_prov";

#define ALIRO_PROV_NS  "aliro_prov"
#define ALIRO_PROV_KEY "blob"

/* Idempotent NVS bring-up. aliro_ble also calls nvs_flash_init() later; the
 * second call returns ESP_OK, so ordering between the two is irrelevant. */
static esp_err_t ensure_nvs(void)
{
	esp_err_t err = nvs_flash_init();

	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	return err;
}

// Load the reader identity and trust store from NVS into id and ts.
// On any failure to init NVS, open the namespace, read the blob, or deserialize it, falls back to the
// default DEV identity (via aliro_prov_dev_default) and returns a nonzero status: 1 if no provisioning
// was ever stored (namespace or key not found), -1 on any other NVS or deserialization error. Returns 0
// on a successful load of previously stored provisioning.
int aliro_prov_load(struct aliro_reader_identity *id,
		    struct aliro_trust_store *ts)
{
	if (ensure_nvs() != ESP_OK) {
		ESP_LOGW(TAG, "NVS init failed; using DEV identity");
		aliro_prov_dev_default(id, ts);
		return -1;
	}

	nvs_handle_t h;
	esp_err_t err = nvs_open(ALIRO_PROV_NS, NVS_READONLY, &h);

	if (err == ESP_ERR_NVS_NOT_FOUND) {
		/* Namespace never written: no provisioning yet. */
		aliro_prov_dev_default(id, ts);
		return 1;
	}
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "nvs_open(ro) err=0x%x; using DEV identity", err);
		aliro_prov_dev_default(id, ts);
		return -1;
	}

	uint8_t buf[ALIRO_PROV_BLOB_MAX];
	size_t sz = sizeof(buf);

	err = nvs_get_blob(h, ALIRO_PROV_KEY, buf, &sz);
	nvs_close(h);

	if (err == ESP_ERR_NVS_NOT_FOUND) {
		aliro_prov_dev_default(id, ts);
		return 1;
	}
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "nvs_get_blob err=0x%x; using DEV identity", err);
		aliro_prov_dev_default(id, ts);
		return -1;
	}
	if (aliro_prov_deserialize(buf, sz, id, ts) != 0) {
		ESP_LOGW(TAG, "stored prov blob malformed (%u B); using DEV identity",
			 (unsigned)sz);
		aliro_prov_dev_default(id, ts);
		return -1;
	}
	return 0;
}

// Serialize and persist the reader identity and trust store to NVS.
// Returns 0 on success. Returns -1 if serialization overflows the blob buffer, NVS init fails, the
// namespace can't be opened read-write, or the blob write/commit fails.
int aliro_prov_store(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts)
{
	uint8_t buf[ALIRO_PROV_BLOB_MAX];
	size_t n;

	if (aliro_prov_serialize(id, ts, buf, sizeof(buf), &n) != 0) {
		return -1;
	}
	if (ensure_nvs() != ESP_OK) {
		return -1;
	}

	nvs_handle_t h;
	esp_err_t err = nvs_open(ALIRO_PROV_NS, NVS_READWRITE, &h);

	if (err != ESP_OK) {
		ESP_LOGW(TAG, "nvs_open(rw) err=0x%x", err);
		return -1;
	}
	err = nvs_set_blob(h, ALIRO_PROV_KEY, buf, n);
	if (err == ESP_OK) {
		err = nvs_commit(h);
	}
	nvs_close(h);

	if (err != ESP_OK) {
		ESP_LOGW(TAG, "nvs write err=0x%x", err);
		return -1;
	}
	ESP_LOGI(TAG, "provisioning persisted (%u B, %u trusted)", (unsigned)n,
		 ts != NULL ? ts->count : 0u);
	return 0;
}
