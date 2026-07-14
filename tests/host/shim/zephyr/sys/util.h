/* Host shim for <zephyr/sys/util.h> — the container/compare macros the pure
 * modules use (ARRAY_SIZE, MIN/MAX, CLAMP, BIT, ...). */
#ifndef WOZ_HOST_SHIM_UTIL_H
#define WOZ_HOST_SHIM_UTIL_H

#include <stddef.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(x, lo, hi) MIN(MAX((x), (lo)), (hi))
#endif
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif
#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif
#ifndef ROUND_UP
#define ROUND_UP(x, align) (DIV_ROUND_UP((x), (align)) * (align))
#endif
#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, field) \
	((type *)(((char *)(ptr)) - offsetof(type, field)))
#endif
#ifndef ARG_UNUSED
#define ARG_UNUSED(x) ((void)(x))
#endif

#endif /* WOZ_HOST_SHIM_UTIL_H */
