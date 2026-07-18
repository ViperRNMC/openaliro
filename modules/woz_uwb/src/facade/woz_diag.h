/** @file woz_diag.h — DIAGK(): compile-time gate for verbose UWB bring-up diagnostics. */

#ifndef WOZ_DIAG_H
#define WOZ_DIAG_H

#include <zephyr/sys/printk.h>

#if defined(CONFIG_WOZ_PRETTY_SHELL)
#define DIAGK(...)                                                                                 \
	do {                                                                                       \
		if (0) {                                                                           \
			printk(__VA_ARGS__);                                                       \
		}                                                                                  \
	} while (0)
#else
#define DIAGK(...) printk(__VA_ARGS__)
#endif

#endif /* WOZ_DIAG_H */
