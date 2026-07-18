/** @file fira_device_config.h — FiRa DS-TWR device/session parameter bag consumed by
 * fira_session.c. */

#ifndef WOZ_UWB_FIRA_DEVICE_CONFIG_H_
#define WOZ_UWB_FIRA_DEVICE_CONFIG_H_

#include <stdint.h>

/** @brief Unicast FiRa ranging device configuration (one session). */
struct fira_device_configure_s {
	uint8_t role;                 /**< 0 = controlee/responder, 1 = controller/initiator. */
	uint8_t enc_payload;          /**< Encrypted-payload flag (unused on the Aliro path). */
	uint32_t Session_ID;          /**< FiRa session id. */
	uint8_t Ranging_Round_Usage;  /**< 2 = DS-TWR (deferred). */
	uint8_t Multi_Node_Mode;      /**< 0 = unicast. */
	uint8_t Rframe_Config;        /**< 3 = SP3 (STS + no PHR payload). */
	uint8_t ToF_Report;           /**< Report time-of-flight (distance). */
	uint8_t AoA_Azimuth_Report;   /**< Angle-of-arrival azimuth report (off). */
	uint8_t AoA_Elevation_Report; /**< AoA elevation report (off). */
	uint8_t AoA_FOM_Report;       /**< AoA figure-of-merit report (off). */
	uint8_t nonDeferred_Mode;     /**< Non-deferred TWR (0 = deferred). */
	uint8_t STS_Config;           /**< STS mode (Static vs Provisioned). */
	uint8_t Round_Hopping;        /**< Round-hopping enable (0 = fixed round). */
	uint8_t Block_Striding;       /**< Block stride length. */
	uint32_t Block_Duration_ms;   /**< Ranging block period, ms. */
	uint32_t Round_Duration_RSTU; /**< Ranging round duration, RSTU. */
	uint32_t Slot_Duration_RSTU;  /**< Ranging slot duration, RSTU. */
	uint8_t Channel_Number;       /**< UWB channel (5 or 9). */
	uint8_t Preamble_Code;        /**< Preamble/SYNC code index (9–12). */
	uint8_t PRF_Mode;             /**< PRF mode (0 = BPRF). */
	uint8_t SP0_PHY_Set;          /**< SP0 PHY set. */
	uint8_t SP1_PHY_Set;          /**< SP1 PHY set. */
	uint8_t SP3_PHY_Set;          /**< SP3 PHY set. */
	uint32_t MAX_RR_Retry;        /**< Max ranging-round retries. */
	uint8_t Constraint_Length_Conv_Code_HPRF; /**< HPRF convolutional-code K (unused on BPRF).
						   */
	uint32_t UWB_Init_Time_ms;                /**< UWB initiation time, ms. */
	uint16_t Block_Timing_Stability;          /**< Block timing stability (ppm). */
	uint8_t Key_Rotation;                     /**< STS key rotation enable. */
	uint8_t Key_Rotation_Rate;                /**< STS key rotation rate. */
	uint8_t MAC_FCS_TYPE;                     /**< MAC FCS type (CRC-16/32). */
	uint8_t MAC_ADDRESS_MODE;                 /**< MAC address mode (short/extended). */
	uint8_t SRC_ADDR[2];                      /**< Local short address, LE. */
	uint8_t Number_of_Controlee;              /**< Controlee count (1 for unicast). */
	uint8_t DST_ADDR[2];                      /**< Peer short address, LE. */

	uint8_t Vendor_ID[2];     /**< Static-STS vUpper64 VendorID (BE into high bytes). */
	uint8_t Static_STS_IV[6]; /**< Static-STS vUpper64 IV (low bytes). */
};

// Typedef alias for struct fira_device_configure_s; holds FiRa device configuration parameters.
typedef struct fira_device_configure_s fira_device_configure_t;

#endif /* WOZ_UWB_FIRA_DEVICE_CONFIG_H_ */
