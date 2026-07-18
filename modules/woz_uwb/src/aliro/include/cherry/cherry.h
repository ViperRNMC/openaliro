/** @file cherry.h — Cherry core (context + device-capabilities) interface. */

#pragma once

#include "cherry_common.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque Cherry context (defined by the shim). */
struct cherry;

/** Core event object (only referenced by pointer in the core callback). */
struct cherry_core_event;

/** Status codes returned across the Cherry / CCC seam. */
enum cherry_err {
	CHERRY_ERR_NONE,
	CHERRY_ERR_INVALID_PARAMETER,
	CHERRY_ERR_UWBS_TIMEOUT,
	CHERRY_ERR_INTERNAL,
	CHERRY_ERR_SESSION_INIT,
	CHERRY_ERR_SESSION_ACTIVE,
	CHERRY_ERR_SESSION_CONFIG,
	CHERRY_ERR_SESSION_TYPE_NOT_SUPPORTED,
};

/**
 * @brief Opaque per-technology FiRa capability blob; unused on this lock since only CCC
 * capabilities are populated.
 */
struct cherry_fira_capabilities;
/**
 * @brief Opaque CCC device capabilities structure, not accessible outside the cherry library.
 */
struct cherry_ccc_capabilities;
/**
 * @brief Opaque radar device capabilities structure, not accessible outside the cherry library.
 */
struct cherry_radar_capabilities;

/**
 * @brief UWBS capability container reported by the peer during device discovery; only the CCC
 * capabilities member is consulted.
 */
struct cherry_core_event_device_capabilities {
	enum cherry_err status_err;
	/**
	 * @brief FiRa capabilities advertised by the peer during device discovery; unused on this
	 * lock.
	 */
	struct cherry_fira_capabilities *fira_capabilities;
	/**
	 * @brief CCC capabilities advertised by the peer during device discovery.
	 */
	struct cherry_ccc_capabilities *ccc_capabilities;
	/**
	 * @brief Radar capabilities advertised by the peer during device discovery.
	 */
	struct cherry_radar_capabilities *radar_capabilities;
};

/**
 * @brief Callback type for core (non-session) Cherry notification events.
 */
typedef void (*cherry_core_cb_t)(struct cherry_core_event *event, void *user_data);

/** Allocate a Cherry context (NULL on allocation failure). */
struct cherry *cherry_create(const char *device, cherry_core_cb_t core_cb, void *user_data);

/**
 * @brief Synchronously release a Cherry context and its resources.
 * @param ctx Cherry context to destroy.
 */
void cherry_destroy_sync(struct cherry *ctx);

#ifdef __cplusplus
}
#endif
