/** @file aliro_uwb_adapter.c — reader-context lifecycle. */

#include "aliro_uwb_internal.h"

#include <aliro_uwb_adapter/aliro_uwb_adapter.h>
#include <cherry/cherry_ccc.h>

#include "woz_alloc.h"
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(woz_aliro_uwb, LOG_LEVEL_INF);

/**
 * @brief Map a CCC error code to its Aliro UWB equivalent, treating unknown errors as internal
 * failures.
 * @param err CCC error code to translate.
 * @return Corresponding `enum aliro_uwb_err` value, or `ALIRO_UWB_ERR_INTERNAL` if unrecognized.
 */
enum aliro_uwb_err cherry_err_to_aliro(enum cherry_err err)
{
	switch (err) {
	case CHERRY_ERR_NONE:
		return ALIRO_UWB_ERR_NONE;
	case CHERRY_ERR_INVALID_PARAMETER:
		return ALIRO_UWB_ERR_INVALID_PARAMETER;
	case CHERRY_ERR_UWBS_TIMEOUT:
		return ALIRO_UWB_ERR_UWBS_TIMEOUT;
	case CHERRY_ERR_INTERNAL:
		return ALIRO_UWB_ERR_INTERNAL;
	case CHERRY_ERR_SESSION_INIT:
		return ALIRO_UWB_ERR_SESSION_INIT;
	case CHERRY_ERR_SESSION_ACTIVE:
		return ALIRO_UWB_ERR_SESSION_ACTIVE;
	case CHERRY_ERR_SESSION_CONFIG:
		return ALIRO_UWB_ERR_SESSION_CONFIG;
	case CHERRY_ERR_SESSION_TYPE_NOT_SUPPORTED:
		return ALIRO_UWB_ERR_INTERNAL;
	}
	return ALIRO_UWB_ERR_INTERNAL;
}

/* Deep-copy the device CCC capabilities into the adapter. */
static enum aliro_uwb_err
/**
 * @brief Deep-copy the device CCC capabilities into the adapter.
 * @param adapter Adapter whose `ccc_caps` field receives the copied capabilities.
 * @param caps Device capabilities event supplying the source CCC capabilities to copy.
 * @return `ALIRO_UWB_ERR_NONE` on success, or `ALIRO_UWB_ERR_INTERNAL` if the source capabilities are missing or allocation fails.
 */
copy_capabilities(struct aliro_uwb_adapter *adapter,
		  struct cherry_core_event_device_capabilities *caps)
{
	struct cherry_ccc_capabilities *src = caps->ccc_capabilities;
	/**
	 * @brief CCC device capabilities reported by the reader, including protocol versions, UWB
	 * configs, and pulse shape combinations.
	 */
	struct cherry_ccc_capabilities *dst = &adapter->ccc_caps;

	if (!src) {
		LOG_ERR("device reports no CCC capabilities");
		return ALIRO_UWB_ERR_INTERNAL;
	}

	*dst = *src;
	dst->protocol_versions.items = NULL;
	dst->uwb_configs.items = NULL;
	dst->pulse_shape_combos.items = NULL;

	dst->protocol_versions.items = qmalloc(sizeof(uint16_t) * src->protocol_versions.len);
	dst->uwb_configs.items = qmalloc(sizeof(uint16_t) * src->uwb_configs.len);
	dst->pulse_shape_combos.items = qmalloc(sizeof(uint8_t) * src->pulse_shape_combos.len);
	if (!dst->protocol_versions.items || !dst->uwb_configs.items ||
	    !dst->pulse_shape_combos.items) {
		qfree(dst->protocol_versions.items);
		qfree(dst->uwb_configs.items);
		qfree(dst->pulse_shape_combos.items);
		return ALIRO_UWB_ERR_INTERNAL;
	}

	memcpy(dst->protocol_versions.items, src->protocol_versions.items,
	       sizeof(uint16_t) * src->protocol_versions.len);
	memcpy(dst->uwb_configs.items, src->uwb_configs.items,
	       sizeof(uint16_t) * src->uwb_configs.len);
	memcpy(dst->pulse_shape_combos.items, src->pulse_shape_combos.items,
	       sizeof(uint8_t) * src->pulse_shape_combos.len);
	return ALIRO_UWB_ERR_NONE;
}

/**
 * @brief Validate that a reader configuration offers at least one valid hopping sequence and
 * respects configured bounds, returning false if invalid.
 * @param config Reader configuration to validate.
 * @return true if the configuration's hopping count is within bounds and includes a default
 * sequence, false otherwise.
 */
static bool reader_config_valid(const struct aliro_uwb_adapter_reader_config *config)
{
	/**
	 * @brief Preferred hopping sequences offered by the reader to a ranging session.
	 */
	const struct aliro_uwb_preferred_hopping_configs *hops = &config->preferred_hopping_configs;
	size_t i;

	if (hops->count == 0 || hops->count > ALIRO_UWB_ADAPTER_PREFERRED_HOP_CONFIG_MAX) {
		LOG_ERR("bad hopping config count %zu", hops->count);
		return false;
	}
	for (i = 0; i < hops->count; i++) {
		if (hops->configs[i] == ALIRO_HOPPING_CONFIG_ADAPTIVE_DEFAULT ||
		    hops->configs[i] == ALIRO_HOPPING_CONFIG_CONTINUOUS_DEFAULT) {
			return true;
		}
	}
	LOG_ERR("no default-sequence hopping config offered");
	return false;
}

struct aliro_uwb_adapter
	*
	/**
	 * @brief Opaque CCC context handle, threaded through to the CCC session API calls.
	 */
	aliro_uwb_adapter_create_reader(
		struct cherry *cherry_ctx,
		/**
		 * @brief Device capabilities event from CCC, containing supported protocol
		 * versions, UWB configurations, and pulse shape combos.
		 */
		struct cherry_core_event_device_capabilities *caps,
		/**
		 * @brief Configuration for an Aliro UWB adapter reader, specifying hopping
		 * preferences, antenna assignments, and RAN multiplier bounds.
		 */
		struct aliro_uwb_adapter_reader_config *config)
{
	struct aliro_uwb_adapter *adapter;

	if (!cherry_ctx || !caps || !config) {
		LOG_ERR("create_reader: null parameter");
		return NULL;
	}
	if (!reader_config_valid(config)) {
		return NULL;
	}

	adapter = qcalloc(1, sizeof(*adapter));
	if (!adapter) {
		return NULL;
	}

	adapter->cherry_ctx = cherry_ctx;
	adapter->config = config;

	if (copy_capabilities(adapter, caps) != ALIRO_UWB_ERR_NONE) {
		qfree(adapter);
		return NULL;
	}

	/* Resolve the minimum RAN multiplier. */
	adapter->min_ran_multiplier =
		config->min_ran_multiplier > adapter->ccc_caps.minimum_ran_multiplier
			? config->min_ran_multiplier
			: adapter->ccc_caps.minimum_ran_multiplier;
	adapter->diag_config = NULL;

	LOG_INF("Aliro adapter created");
	return adapter;
}

/**
 * @brief Store a diagnostics configuration in the adapter for later application to CCC sessions,
 * allocating storage if needed.
 * @param aliro_ctx Adapter that receives the diagnostics configuration.
 * @param config Common diagnostic configuration applied to a CCC session.
 */
void aliro_uwb_adapter_set_diagnostics(
	struct aliro_uwb_adapter *aliro_ctx,
	/**
	 * @brief Common diagnostic configuration applied to a CCC session.
	 */
	struct cherry_common_diag_cfg config)
{
	if (!aliro_ctx) {
		return;
	}
	if (!aliro_ctx->diag_config) {
		aliro_ctx->diag_config = qmalloc(sizeof(config));
		if (!aliro_ctx->diag_config) {
			return;
		}
	}
	memcpy(aliro_ctx->diag_config, &config, sizeof(config));
}

/**
 * @brief Destroy an Aliro UWB adapter, freeing all associated CCC capabilities arrays and
 * diagnostic configuration.
 * @param aliro_ctx Adapter to destroy; no-op if NULL.
 */
void aliro_uwb_adapter_destroy(struct aliro_uwb_adapter *aliro_ctx)
{
	if (!aliro_ctx) {
		return;
	}
	qfree(aliro_ctx->ccc_caps.protocol_versions.items);
	qfree(aliro_ctx->ccc_caps.uwb_configs.items);
	qfree(aliro_ctx->ccc_caps.pulse_shape_combos.items);
	qfree(aliro_ctx->diag_config);
	qfree(aliro_ctx);
}
