/** @file uwb_rxdiag.h — Read-side accessors for the RX event tallies + log stream. */

#ifndef WOZ_UWB_RXDIAG_H_
#define WOZ_UWB_RXDIAG_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Snapshot the running RX/TX event tallies; out-params optional (NULL to skip). */
void uwb_rxdiag_get_counts(uint32_t *rxok, uint32_t *rxerr, uint32_t *rxto, uint32_t *txdone,
			   uint32_t *last_err, uint32_t *last_ok);

/** @brief Arm or cancel the periodic ranging heartbeat (backs `aliro log on|off`). */
void uwb_rxdiag_stream_set(bool on);

/** @brief Whether the periodic ranging heartbeat is currently armed. */
bool uwb_rxdiag_stream_get(void);

/** @brief Arm or cancel the per-block distance stream (backs `aliro frames on|off`). */
void uwb_rxdiag_rng_set(bool on);

/** @brief Whether the per-block distance stream is currently armed. */
bool uwb_rxdiag_rng_get(void);

#endif /* WOZ_UWB_RXDIAG_H_ */
