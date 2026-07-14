#ifndef DW3000_HW_H
#define DW3000_HW_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

int dw3000_hw_init(void);
int dw3000_hw_init_interrupt(void);
void dw3000_hw_fini(void);
void dw3000_hw_reset(void);
void dw3000_hw_wakeup(void);
void dw3000_hw_wakeup_pin_low(void);
/* Path-A: sleep-state coordination between qpwr_uwb_sleep (which calls
 * dwt_entersleep) and the CS-wake in dw3000_hw_wakeup. */
void dw3000_hw_mark_asleep(void);
bool dw3000_hw_is_asleep(void);
void dw3000_hw_interrupt_enable(void);
void dw3000_hw_interrupt_disable(void);
bool dw3000_hw_interrupt_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
