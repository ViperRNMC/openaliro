/* Minimal ESP-IDF port of the essential RX-callback shim.
 *
 * The Nordic build routes DW3000 RX events through uwb_rxdiag.c's
 * __wrap_dwt_setcallbacks -> shim_rxok, which (after the blob's own
 * prepoll_rx_rearm arms the SP3 POLL window) calls ccc_shim_rx_try_prepoll to
 * decrypt+warm the NEXT block's STS.  That bootstrap warm is what flips
 * g_warm_valid true so the POLL window ever gets armed and Response_0 sent.
 *
 * This port omits uwb_rxdiag.c wholesale (its heartbeat needs Zephyr k_work,
 * which the compat layer does not provide), so without this shim dwt_setcallbacks
 * installs prepoll_rx_rearm directly, ccc_shim_rx_try_prepoll is never reached,
 * g_warm_valid stays false, and the responder receives Pre-POLLs but never
 * replies.  Re-create only the essential chain here (no k_work, no diagnostics).
 *
 * Also keeps the dwt_configurestsmode pass-through the essential RX path needs. */
#include <stdint.h>

#include <deca_device_api.h>

#include "ccc_shim.h" /* ccc_shim_rx_{awaiting_poll,notify_rx,try_prepoll} */

void __real_dwt_configurestsmode(uint8_t stsMode);

// Wrapped __real_dwt_configurestsmode with no added behavior; forwards stsMode unchanged.
void __wrap_dwt_configurestsmode(uint8_t stsMode)
{
	__real_dwt_configurestsmode(stsMode);
}

/* Diagnostic decode-latency counter owned by the omitted uwb_rxdiag.c; ccc_shim_rx.c
 * reads it (extern) only in DIAG log lines. Define it here so those references
 * resolve (stays 0 without the rxdiag stamper — diagnostics only). */
uint32_t g_ccc_dbg_decode;

/* The real registration, reachable past the ld --wrap. */
void __real_dwt_setcallbacks(dwt_callbacks_s *callbacks);

/* The blob's own callbacks, saved so our shims chain to them. */
static dwt_cb_t g_blob_rxok, g_blob_rxto, g_blob_rxerr, g_blob_txdone;

/* RX-good shim: feed the empirical STS-index tracker, run the blob's arm
 * (prepoll_rx_rearm), then — unless this RX is the awaited POLL — decode the
 * Pre-POLL to warm the next block's STS.  Mirrors uwb_rxdiag.c:shim_rxok minus
 * the tallies/cadence/event logging. */
static void shim_rxok(const dwt_cb_data_t *d)
{
	bool await = ccc_shim_rx_awaiting_poll();

	if (d != NULL) {
		ccc_shim_rx_notify_rx(d->status);
	}
	if (g_blob_rxok != NULL) {
		g_blob_rxok(d);
	}
	if (d != NULL && !await) {
		ccc_shim_rx_try_prepoll(d->datalength);
	}
}

// RX-timeout callback shim; forwards the event to g_blob_rxto if a handler is registered, otherwise no-op.
static void shim_rxto(const dwt_cb_data_t *d)
{
	if (g_blob_rxto != NULL) {
		g_blob_rxto(d);
	}
}

// RX-error callback shim; forwards the event to g_blob_rxerr if a handler is registered, otherwise no-op.
static void shim_rxerr(const dwt_cb_data_t *d)
{
	if (g_blob_rxerr != NULL) {
		g_blob_rxerr(d);
	}
}

// TX-done callback shim; forwards the event to g_blob_txdone if a handler is registered, otherwise no-op.
static void shim_txdone(const dwt_cb_data_t *d)
{
	if (g_blob_txdone != NULL) {
		g_blob_txdone(d);
	}
}

/* Intercept the callback registration and insert the RX-good bootstrap shim. */
void __wrap_dwt_setcallbacks(dwt_callbacks_s *callbacks)
{
	if (callbacks != NULL) {
		g_blob_rxok = callbacks->cbRxOk;
		g_blob_rxto = callbacks->cbRxTo;
		g_blob_rxerr = callbacks->cbRxErr;
		g_blob_txdone = callbacks->cbTxDone;
		callbacks->cbRxOk = (g_blob_rxok != NULL) ? shim_rxok : NULL;
		callbacks->cbRxTo = (g_blob_rxto != NULL) ? shim_rxto : NULL;
		callbacks->cbRxErr = (g_blob_rxerr != NULL) ? shim_rxerr : NULL;
		callbacks->cbTxDone = (g_blob_txdone != NULL) ? shim_txdone : NULL;
	}
	__real_dwt_setcallbacks(callbacks);
}
