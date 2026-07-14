/* Host shim for <zephyr/sys/printk.h> — map printk/snprintk onto stdio. */
#ifndef WOZ_HOST_SHIM_PRINTK_H
#define WOZ_HOST_SHIM_PRINTK_H

#include <stdio.h>

#define printk(...)   printf(__VA_ARGS__)
#define snprintk(...) snprintf(__VA_ARGS__)
#define vsnprintk(...) vsnprintf(__VA_ARGS__)

#endif /* WOZ_HOST_SHIM_PRINTK_H */
