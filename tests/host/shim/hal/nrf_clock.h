/* Host shim for <hal/nrf_clock.h> — the one HFCLK call the facade makes,
 * a no-op off-target. */
#ifndef WOZ_HOST_SHIM_NRF_CLOCK_H
#define WOZ_HOST_SHIM_NRF_CLOCK_H

#define NRF_CLOCK              ((void *)0)
#define NRF_CLOCK_HFCLK_DIV_1  0

static inline void nrf_clock_hfclk_div_set(void *clock, int div)
{
	(void)clock;
	(void)div;
}

#endif /* WOZ_HOST_SHIM_NRF_CLOCK_H */
