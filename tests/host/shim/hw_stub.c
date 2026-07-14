/* Host no-op stubs for the DW3000 RX responder entry points that the facade
 * calls but that live in the excluded hardware file ccc_shim_rx.c. They only
 * need to link; on the host there is no radio to arm or log to reset. If a unit
 * test later drives the facade, it observes these as inert. */
#include <stdint.h>

#include "ccc_shim.h"

void ccc_shim_rx_log_reset(void)
{
}

int ccc_prepoll_listen(uint8_t channel, uint8_t preamble_code)
{
	(void)channel;
	(void)preamble_code;
	return 0;
}
