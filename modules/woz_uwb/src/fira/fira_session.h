/** @file fira_session.h — Range + URSK store for the CCC Pre-POLL responder. */

#ifndef WOZ_UWB_FIRA_SESSION_H_
#define WOZ_UWB_FIRA_SESSION_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Fetch the most recent valid DS-TWR range; out-params optional (NULL to skip). */
bool fira_session_last_range(int32_t *cm_out, uint16_t *addr_out,
			     uint8_t *nlos_out, uint32_t *block_out,
			     int64_t *age_ms_out);

#if defined(CONFIG_WOZ_ALIRO)
/** @brief Stash an Aliro URSK for the CCC Pre-POLL STS decode; NULL clears it. */
void fira_session_set_provisioned_ursk(const uint8_t *ursk);

/** @brief The stashed Aliro URSK (32 bytes), or NULL if none — for the Pre-POLL decode. */
const uint8_t *fira_session_get_ursk(void);

/** @brief Latch a CCC DS-TWR range so it flows up the Aliro mRangingData seam. */
void fira_session_set_ccc_range_cm(int32_t cm, uint32_t block);

/** @brief STS-index slot clock (inert without a MAC time base); returns 0. */
uint32_t fira_session_current_slot(void);
#endif /* CONFIG_WOZ_ALIRO */

#endif /* WOZ_UWB_FIRA_SESSION_H_ */
