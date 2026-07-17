/** @file test_prepoll_gate.c — ccc_shim_rx listen-gate: stop kills every RX
 * self-rearm path, listen reopens it, and the radio's last act on stop is the
 * forcetrxoff (the serialization invariant the suspend fix relies on). The
 * captured DW3000 callbacks are invoked directly, playing the role of the
 * coop isr-workqueue thread. */
#include <string.h>

#include <deca_device_api.h>

#include "ccc_shim.h"
#include "test.h"

/* Drive a captured RX callback the way dwt_isr would. */
static void rx_event(dwt_cb_t cb, uint32_t status)
{
	dwt_cb_data_t d;

	memset(&d, 0, sizeof(d));
	d.status = status;
	cb(&d);
}

void test_prepoll_gate(void)
{
	unsigned before;

	t_group("stop before any listen is a no-op (driver may be unprobed)");
	woz_host_rx_reset();
	ccc_prepoll_stop();
	T_EQ("stop.cold.no_trxoff", woz_host_rx.forcetrxoff_calls, 0);
	T_EQ("stop.cold.no_rxenable", woz_host_rx.rxenable_calls, 0);

	t_group("listen arms RX and installs the self-rearm callbacks");
	woz_host_rx_reset();
	T_EQ("listen.rc", ccc_prepoll_listen(9u, 9u), 0);
	T_EQ("listen.armed", woz_host_rx.rxenable_calls, 1);
	T_EQ("listen.arm.immediate", woz_host_rx.last_rxenable_mode,
	     DWT_START_RX_IMMEDIATE);
	T_OK("listen.cb.rxok", woz_host_rx.cbs.cbRxOk != NULL);
	T_OK("listen.cb.rxto", woz_host_rx.cbs.cbRxTo != NULL);
	T_OK("listen.cb.rxerr", woz_host_rx.cbs.cbRxErr != NULL);
	T_OK("listen.cb.txdone", woz_host_rx.cbs.cbTxDone != NULL);

	t_group("while up, every RX outcome self-rearms");
	before = woz_host_rx.rxenable_calls;
	rx_event(woz_host_rx.cbs.cbRxOk, 0u); /* quiet event -> SP0 fallback */
	T_EQ("up.rxok.rearms", woz_host_rx.rxenable_calls, before + 1);
	rx_event(woz_host_rx.cbs.cbRxTo, DWT_INT_RXFTO_BIT_MASK);
	T_EQ("up.rxto.rearms", woz_host_rx.rxenable_calls, before + 2);
	rx_event(woz_host_rx.cbs.cbRxErr, DWT_INT_RXFCE_BIT_MASK);
	T_EQ("up.rxerr.rearms", woz_host_rx.rxenable_calls, before + 3);

	t_group("stop closes the gate: no callback path re-enables RX");
	before = woz_host_rx.forcetrxoff_calls; /* listen()'s own trxoff is already in */
	ccc_prepoll_stop();
	T_EQ("stop.trxoff", woz_host_rx.forcetrxoff_calls, before + 1);
	before = woz_host_rx.rxenable_calls;
	rx_event(woz_host_rx.cbs.cbRxOk, 0u);
	rx_event(woz_host_rx.cbs.cbRxOk, DWT_INT_RXFCG_BIT_MASK |
					 DWT_INT_RXPHD_BIT_MASK |
					 DWT_INT_CIADONE_BIT_MASK);
	rx_event(woz_host_rx.cbs.cbRxTo, DWT_INT_RXFTO_BIT_MASK);
	rx_event(woz_host_rx.cbs.cbRxErr, DWT_INT_RXFCE_BIT_MASK);
	T_EQ("stop.rx.gated", woz_host_rx.rxenable_calls, before);

	t_group("late TXFRS after stop cannot rearm the Final window");
	before = woz_host_rx.rxenable_calls;
	rx_event(woz_host_rx.cbs.cbTxDone, DWT_INT_TXFRS_BIT_MASK);
	T_EQ("stop.txdone.gated", woz_host_rx.rxenable_calls, before);

	t_group("after stop the radio's last act is the forcetrxoff");
	T_OK("stop.trxoff.last",
	     woz_host_rx.last_forcetrxoff_seq > woz_host_rx.last_rxenable_seq);

	t_group("double stop is idempotent");
	before = woz_host_rx.forcetrxoff_calls;
	ccc_prepoll_stop();
	T_EQ("stop.twice.no_trxoff", woz_host_rx.forcetrxoff_calls, before);

	t_group("resume: listen reopens the gate and the rearm loop");
	T_EQ("resume.rc", ccc_prepoll_listen(9u, 9u), 0);
	before = woz_host_rx.rxenable_calls;
	T_OK("resume.armed", before > 0);
	rx_event(woz_host_rx.cbs.cbRxOk, 0u);
	T_EQ("resume.rxok.rearms", woz_host_rx.rxenable_calls, before + 1);

	/* Leave the listener stopped so later suites see a quiet radio. */
	ccc_prepoll_stop();
}
