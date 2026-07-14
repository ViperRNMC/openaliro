/** @file uwb_isr.h — DW3000 interrupt-callback registration (public surface). */

#ifndef WOZ_UWB_ISR_H_
#define WOZ_UWB_ISR_H_

/** @brief Install RX/TX callbacks and unmask the SYS_ENABLE bits; returns 0. */
int uwb_isr_register(void);

#endif /* WOZ_UWB_ISR_H_ */
