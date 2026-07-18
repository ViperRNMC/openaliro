/** @file cherry_session.h — generic base-session interface. */

#pragma once

#include <cherry/cherry.h>
#include <cherry/cherry_common.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque generic session (defined by the shim; first member of a CCC one). */
struct cherry_session;

/** Return the opaque client pointer stored at session creation. */
void *cherry_session_get_user_data(struct cherry_session *session);

/** Stop if needed, tear down, and release the session. */
void cherry_session_destroy(struct cherry_session *session);

/** Request the session to start ranging. */
enum cherry_err cherry_session_start(struct cherry_session *session);

/** Request the session to stop ranging. */
enum cherry_err cherry_session_stop(struct cherry_session *session);

/** Select the Tx/Rx antenna sets for the first ranging round. */
enum cherry_err cherry_session_set_antennas(struct cherry_session *session, uint8_t tx_antenna_set,
					    uint8_t rx_antenna_set);

/** Enable/disable per-session diagnostics (config passed by value). */
enum cherry_err
/**
 * @brief Opaque generic session pointer (defined by the shim; first member of a CCC session).
 */
cherry_session_set_diagnostics(struct cherry_session *session,
			       /**
			        * @brief Common diagnostic configuration to apply to the CCC session.
			        */
			       struct cherry_common_diag_cfg config,
			       bool enable_fallback);

#ifdef __cplusplus
}
#endif
