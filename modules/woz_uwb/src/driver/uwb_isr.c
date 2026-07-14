/** @file uwb_isr.c — DW3000 interrupt-callback registration (implementation). */

#include "uwb_isr.h"

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <deca_device_api.h>

#include "trace.h"

LOG_MODULE_REGISTER(uwb_isr, LOG_LEVEL_INF);

/* Up to 8 bytes of the frame body are pulled into the trace to identify the frame. */
#define ISR_FRAME_PEEK_BYTES 8

/** @brief RX-good callback: peek the frame header, log via WOZ_TRACE, then re-arm RX. */
static void cb_rx_ok(const dwt_cb_data_t *d)
{
	uint8_t peek[ISR_FRAME_PEEK_BYTES] = {0};
	const uint16_t flen = d->datalength;
	const uint16_t copy = (flen > ISR_FRAME_PEEK_BYTES) ? ISR_FRAME_PEEK_BYTES : flen;
	if (copy > 0) {
		dwt_readrxdata(peek, copy, 0);
	}

	WOZ_TRACE("uwb.rx.ok",
		   "len=%u status=0x%08X "
		   "head=%02X%02X%02X%02X%02X%02X%02X%02X",
		   (unsigned)flen, (unsigned int)d->status,
		   peek[0], peek[1], peek[2], peek[3],
		   peek[4], peek[5], peek[6], peek[7]);

	(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

/** @brief RX-timeout callback: frame-wait window expired; re-arms RX. */
static void cb_rx_to(const dwt_cb_data_t *d)
{
	WOZ_TRACE("uwb.rx.to", "status=0x%08X", (unsigned int)d->status);
	(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

/** @brief RX-error callback: frame heard but rejected; logs status and re-arms RX. */
static void cb_rx_err(const dwt_cb_data_t *d)
{
	WOZ_TRACE("uwb.rx.err", "status=0x%08X", (unsigned int)d->status);
	(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

/** @brief TX-done callback: our transmitted frame left the antenna. */
static void cb_tx_done(const dwt_cb_data_t *d)
{
	WOZ_TRACE("uwb.tx.done", "status=0x%08X", (unsigned int)d->status);
}

int uwb_isr_register(void)
{
	static dwt_callbacks_s callbacks;

	callbacks.cbTxDone    = cb_tx_done;
	callbacks.cbRxOk      = cb_rx_ok;
	callbacks.cbRxTo      = cb_rx_to;
	callbacks.cbRxErr     = cb_rx_err;
	callbacks.cbSPIErr    = NULL;
	callbacks.cbSPIRDErr  = NULL;
	callbacks.cbSPIRdy    = NULL;
	callbacks.cbDualSPIEv = NULL;
	dwt_setcallbacks(&callbacks);

	/* Unmask the events we registered handlers for; high-half bits stay 0. */
	const uint32_t lo_mask =
		DWT_INT_RXFCG_BIT_MASK |  /* good frame */
		DWT_INT_RXFTO_BIT_MASK |  /* frame-wait timeout */
		DWT_INT_RXPTO_BIT_MASK |  /* preamble timeout */
		DWT_INT_RXPHE_BIT_MASK |  /* PHR error */
		DWT_INT_RXFCE_BIT_MASK |  /* CRC error */
		DWT_INT_RXFSL_BIT_MASK |  /* Reed-Solomon error */
		DWT_INT_RXSTO_BIT_MASK |  /* SFD timeout */
		DWT_INT_ARFE_BIT_MASK  |  /* frame-filter error */
		DWT_INT_CIAERR_BIT_MASK|  /* STS authentication failure */
		DWT_INT_TXFRS_BIT_MASK;   /* TX done */

	dwt_setinterrupt(lo_mask, 0u, DWT_ENABLE_INT);

	LOG_INF("uwb_isr: callbacks registered, SYS_ENABLE mask=0x%08X",
		(unsigned int)lo_mask);
	return 0;
}
