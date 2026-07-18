/** @file uwb_min.h — Minimal DW3110 (DWM3000EVB) hardware bring-up driver. */

#ifndef WOZ_UWB_MIN_H_
#define WOZ_UWB_MIN_H_

#include <stdbool.h>
#include <stdint.h>

/** Expected DEV_ID value for the DW3110 (the IC on the DWM3000 module). */
#define UWB_DW3110_DEV_ID 0xDECA0302u

/** @brief Self-test result emitted by @ref uwb_min_selftest. */
struct uwb_selftest_result {
	bool tx_done;       /**< SYS_STATUS TXFRS bit observed after TX. */
	bool rx_armed;      /**< dwt_rxenable() returned success. */
	bool rx_event;      /**< Some RX status bit fired (frame, timeout, or error). */
	uint32_t tx_status; /**< SYS_STATUS captured at end of TX poll. */
	uint32_t rx_status; /**< SYS_STATUS captured at end of RX poll. */
};

/** @brief Read the DW3000-family DEV_ID register over SPI. */
int uwb_min_read_chipid(uint32_t *id_out);

/** @brief Pulse the DW3110 RST line low to force a hardware reset. */
int uwb_min_hw_reset(void);

/** @brief Radio self-test: configure, TX one frame, then arm RX. */
int uwb_min_selftest(struct uwb_selftest_result *out);

/** @brief Result of a raw static-STS SS-TWR initiator burst (@ref uwb_min_twr_poll). */
struct uwb_twr_result {
	uint32_t polls_tx;    /**< POLL frames sent (TXFRS observed). */
	uint32_t resp_rx;     /**< RESP frames received (any non-timeout RX event). */
	uint32_t sts_ok;      /**< RESPs whose STS quality index was > 0. */
	int16_t last_sts;     /**< STS quality index of the last RESP. */
	uint32_t last_status; /**< SYS_STATUS captured on the last RX. */
};

/** @brief Outcome of one raw SS-TWR POLL/RESP exchange (@ref uwb_min_twr_exchange). */
struct uwb_twr_frame {
	bool tx_ok;      /**< POLL left the antenna (TXFRS seen). */
	bool timed_out;  /**< RX window closed with no RESP frame. */
	int16_t sts;     /**< STS quality index of the RESP (>0 = correlated). */
	uint32_t status; /**< SYS_STATUS captured on the RX. */
};

/** @brief Configure the radio for the raw SS-TWR loopback (SP3-ND, ch9, code11); no STS. */
int uwb_min_twr_prep(void);

/** @brief Run one POLL/RESP exchange; the STS must already be programmed. */
void uwb_min_twr_exchange(struct uwb_twr_frame *f);

/** @brief Raw static-STS SS-TWR initiator burst (bench probe). */
int uwb_min_twr_poll(uint32_t n, uint32_t period_ms, struct uwb_twr_result *out);

/** @brief Ensure the DW3110 is fully initialised (probe + initialise + configure + LEDs). */
int uwb_min_radio_init(void);

#endif /* WOZ_UWB_MIN_H_ */
