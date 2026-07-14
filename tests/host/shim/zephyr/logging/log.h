/* Host shim for <zephyr/logging/log.h> — logging compiled out to no-ops.
 * Lets the pure-logic modules build off-target; log calls have no side effects
 * that a unit test observes. Not a functional logging backend. */
#ifndef WOZ_HOST_SHIM_LOG_H
#define WOZ_HOST_SHIM_LOG_H

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERR  1
#define LOG_LEVEL_WRN  2
#define LOG_LEVEL_INF  3
#define LOG_LEVEL_DBG  4

#define LOG_MODULE_REGISTER(...)
#define LOG_MODULE_DECLARE(...)

#define LOG_ERR(...)          ((void)0)
#define LOG_WRN(...)          ((void)0)
#define LOG_INF(...)          ((void)0)
#define LOG_DBG(...)          ((void)0)
#define LOG_PRINTK(...)       ((void)0)

#define LOG_HEXDUMP_ERR(...)  ((void)0)
#define LOG_HEXDUMP_WRN(...)  ((void)0)
#define LOG_HEXDUMP_INF(...)  ((void)0)
#define LOG_HEXDUMP_DBG(...)  ((void)0)

#endif /* WOZ_HOST_SHIM_LOG_H */
