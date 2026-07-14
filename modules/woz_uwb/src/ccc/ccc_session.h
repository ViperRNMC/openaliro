/** @file ccc_session.h — Aliro/CCC ranging seam: map an Aliro session's URSK + M1-M4 setup to ccc_ran_params. */

#ifndef CCC_SESSION_H
#define CCC_SESSION_H

#include <stdint.h>

#include "ccc_mac.h" /* struct ccc_ran_params, enum ccc_hop_mode, CCC_URSK_LEN */

/** SP0 MHR Vendor OUI for Aliro ranging (CSA Company Id). */
#define CCC_ALIRO_VENDOR_OUI 0x4A191Bu

/** An Aliro ranging session: the URSK + the M1-M4 setup parameters; the interface to the CCC MAC. */
struct ccc_ran_session {
	uint8_t  ursk[CCC_URSK_LEN]; /**< Ranging secret key (aliro_kdf). */
	uint32_t uwb_session_id;     /**< M1: UWB Session Identifier. */
	uint32_t sts_index0;         /**< M4: STS_Index0. */
	uint32_t uwb_time0;          /**< M4: UWB_Time0 block-time anchor. */
	uint32_t hop_key_rw;         /**< M4: Hop Mode Key (HOP_Key_RW). */
	uint16_t mac_mode_offset;    /**< M3: MAC Mode round offset O^k (F = H + O). */
	uint8_t  n_ran_s;            /**< M3: selected RAN multiplier N_RAN_S. */
	uint8_t  n_chap_per_slot;    /**< M3: N_Chap_per_Slot. */
	uint8_t  n_responder;        /**< M3: N_Responder. */
	uint8_t  n_slot_per_round;   /**< M3: N_Slot_per_Round. */
	enum ccc_hop_mode hop_mode;  /**< M2/M3: hopping configuration. */
};

/** Rounds per ranging block, from N_RAN_S and the slot parameters (0 if the denominator is 0). */
uint16_t ccc_session_n_round(const struct ccc_ran_session *s);

/** Map an Aliro session onto the CCC MAC's ranging-schedule parameters. */
int ccc_session_to_ran_params(const struct ccc_ran_session *s,
			      // RAN (Random Access Number) parameters: multiplier, index, and preamble code, populated by CCC during session setup.
			      struct ccc_ran_params *out);

#endif /* CCC_SESSION_H */
