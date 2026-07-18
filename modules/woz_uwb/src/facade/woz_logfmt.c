/** @file woz_logfmt.c — PRETTY-gated high-res timestamp + compact colored log line. */

#include <stdint.h>
#include <stddef.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/cbprintf.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_output_custom.h>

#include <cmsis_core.h> /* DWT, CoreDebug — the cycle counter */

/* CMSIS system globals, restated to avoid depending on the include path. */
extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);

/* --- ANSI palette ---------------------------------------------------------- */
#define A_RST    "\x1b[0m"
#define A_DIM    "\x1b[2m"
#define A_BANNER "\x1b[1;97m" /* bold bright-white: source-less boot banner */
#define A_RED    "\x1b[31m"
#define A_YEL    "\x1b[33m"

/* Per-module colours (skip red/yellow); hashed from the source name for stability. */
static const char *const module_colors[] = {
	"\x1b[36m", /* cyan */
	"\x1b[32m", /* green */
	"\x1b[35m", /* magenta */
	"\x1b[34m", /* blue */
	"\x1b[96m", /* bright cyan */
	"\x1b[92m", /* bright green */
};
#define N_MODULE_COLORS ARRAY_SIZE(module_colors)

/* --- DWT-backed 64-bit timestamp ------------------------------------------- */
static uint32_t g_freq;     /* DWT tick rate (== CPU clock), Hz */
static uint32_t g_last_cyc; /* last DWT->CYCCNT sample */
static uint64_t g_acc_cyc;  /* wrap-free 64-bit cycle accumulator */

/** @brief Advance + read the 64-bit cycle accumulator (irq-safe; any context). */
static log_timestamp_t woz_timestamp_get(void)
{
	unsigned int key = irq_lock();
	uint32_t now = DWT->CYCCNT;

	g_acc_cyc += (uint32_t)(now - g_last_cyc); /* uint32 wrap-safe over one wrap */
	g_last_cyc = now;
	log_timestamp_t v = (log_timestamp_t)g_acc_cyc;

	irq_unlock(key);
	return v;
}

/** @brief Periodic sampler so a CYCCNT wrap is never missed during console idle. */
static void woz_wrap_sample(struct k_timer *t)
{
	ARG_UNUSED(t);
	(void)woz_timestamp_get();
}
static K_TIMER_DEFINE(g_wrap_timer, woz_wrap_sample, NULL);

/* --- Custom line formatter ------------------------------------------------- */
/** @brief Small bounded sink so cbpprintf can render into a stack buffer. */
struct woz_sink {
	char *p;
	size_t rem; /* bytes left, always keeps room for no NUL (we track length) */
};

// Sink callback for formatted output: append one character to the buffer if space remains.
static int woz_sink_out(int c, void *ctx)
{
	struct woz_sink *s = ctx;

	if (s->rem > 0u) {
		*s->p++ = (char)c;
		s->rem--;
	}
	return c;
}

// Append a null-terminated string to the sink buffer one character at a time, stopping at end of
// string or exhausted buffer.
static void woz_sink_str(struct woz_sink *s, const char *str)
{
	while (*str != '\0' && s->rem > 0u) {
		*s->p++ = *str++;
		s->rem--;
	}
}

/** @brief Stable per-module colour from a name hash (djb2). */
static const char *module_color(const char *name)
{
	uint32_t h = 5381u;

	if (name == NULL) {
		return module_colors[0];
	}
	for (const char *c = name; *c != '\0'; c++) {
		h = (h * 33u) ^ (uint8_t)*c;
	}
	return module_colors[h % N_MODULE_COLORS];
}

/** @brief Render one message as `SEC.NS module message`, or delegate hexdumps. */
static void woz_msg_format(const struct log_output *output, struct log_msg *msg, uint32_t flags)
{
	size_t dlen = 0;
	uint8_t *data = log_msg_get_data(msg, &dlen);

	size_t plen = 0;
	uint8_t *package = log_msg_get_package(msg, &plen);
	if (plen == 0u) {
		log_output_msg_process(output, msg, flags);
		return;
	}

	bool color = (flags & LOG_OUTPUT_FLAG_COLORS) != 0u;
	uint8_t level = log_msg_get_level(msg);
	int16_t sid = log_msg_get_source_id(msg);
	uint8_t did = log_msg_get_domain(msg);
	const char *name = (sid >= 0) ? log_source_name_get(did, sid) : NULL;

	const char *tagc = module_color(name);
	if (level == LOG_LEVEL_ERR) {
		tagc = A_RED;
	} else if (level == LOG_LEVEL_WRN) {
		tagc = A_YEL;
	}

	/* Cycles -> whole seconds + nanoseconds within the second. */
	uint64_t ts = (uint64_t)log_msg_get_timestamp(msg);
	uint64_t sec = (g_freq != 0u) ? (ts / g_freq) : 0u;
	uint32_t ns = (g_freq != 0u) ? (uint32_t)(((ts % g_freq) * 1000000000ULL) / g_freq) : 0u;

	char line[320];
	struct woz_sink s = {line, sizeof(line)};
	char pre[48];

	snprintk(pre, sizeof(pre), "%llu.%09u ", (unsigned long long)sec, ns);
	if (color) {
		woz_sink_str(&s, A_DIM);
	}
	woz_sink_str(&s, pre);
	if (color) {
		woz_sink_str(&s, A_RST);
	}
	/* Source-less lines (raw printk banners) carry no module: skip the tag
	 * entirely rather than print a bare "?", and render the body bold-bright
	 * so the three boot banner lines read as a header block. */
	bool banner = (name == NULL);
	if (name != NULL) {
		if (color) {
			woz_sink_str(&s, tagc);
		}
		woz_sink_str(&s, name);
		if (color) {
			woz_sink_str(&s, A_RST);
		}
		woz_sink_str(&s, " ");
	} else if (color) {
		woz_sink_str(&s, A_BANNER);
	}

	if (banner) {
		/* Zephyr wraps every boot banner line in "*** … ***". Render the
		 * body to a scratch buffer and drop that wrapper before emitting. */
		char body[256];
		struct woz_sink bs = {body, sizeof(body)};
		cbpprintf(woz_sink_out, &bs, package);

		char *bp = body;
		size_t blen = (size_t)(bs.p - body);
		while (blen > 0u && (bp[blen - 1] == '\n' || bp[blen - 1] == '\r')) {
			blen--; /* defensive: strip any trailing newline first */
		}
		if (blen >= 4u && bp[0] == '*' && bp[1] == '*' && bp[2] == '*' && bp[3] == ' ') {
			bp += 4;
			blen -= 4u;
		}
		if (blen >= 4u && bp[blen - 1] == '*' && bp[blen - 2] == '*' &&
		    bp[blen - 3] == '*' && bp[blen - 4] == ' ') {
			blen -= 4u;
		}
		for (size_t i = 0; i < blen && s.rem > 0u; i++) {
			*s.p++ = bp[i];
			s.rem--;
		}
	} else {
		cbpprintf(woz_sink_out, &s, package);
	}

	if (banner && color) {
		woz_sink_str(&s, A_RST);
	}

	/* Inline any hexdump payload after the message, keeping the pretty
	 * header instead of delegating the whole line to the stock renderer. */
	for (size_t i = 0; i < dlen && s.rem > 3u; i++) {
		char hex[4];

		snprintk(hex, sizeof(hex), " %02x", data[i]);
		woz_sink_str(&s, hex);
	}

	woz_sink_str(&s, "\r\n");

	log_output_write(output->func, (uint8_t *)line, (size_t)(s.p - line),
			 output->control_block->ctx);
}

/* --- Init ------------------------------------------------------------------ */
static int woz_logfmt_init(void)
{
	/* Enable the DWT cycle counter. */
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0u;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	SystemCoreClockUpdate();
	g_freq = (SystemCoreClock != 0u) ? SystemCoreClock : 64000000u;
	g_last_cyc = DWT->CYCCNT;
	g_acc_cyc = 0u;

	(void)log_set_timestamp_func(woz_timestamp_get, g_freq);
	log_custom_output_msg_set(woz_msg_format);

	/* Set the custom format on every backend, not only the active ones:
	 * at this init phase a target backend may not be active yet, and
	 * log_format_set_all_active_backends() silently skips inactive ones.
	 * Backends that don't support format_set just return -ENOTSUP. */
	for (int i = 0; i < log_backend_count_get(); i++) {
		(void)log_backend_format_set(log_backend_get(i), LOG_OUTPUT_CUSTOM);
	}

	k_timer_start(&g_wrap_timer, K_SECONDS(16), K_SECONDS(16));
	return 0;
}

SYS_INIT(woz_logfmt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
