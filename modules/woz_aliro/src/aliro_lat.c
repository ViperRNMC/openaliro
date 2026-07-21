// Walk-up latency trace: first-hit phase timestamps + the consolidated budget line.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_lat — see aliro_lat.h. Marks come from three tasks (BLE host, UWB RX,
 * reader/unlock); each phase is written once per walk-up and only read by the
 * report after the bolt is driven, so plain stores are sufficient for a
 * diagnostic trace.
 */
#include <stdint.h>
#include <string.h>

#include "woz_port.h"
#include "woz_log.h"

#include "aliro_lat.h"

/* 0 = unmarked this walk-up (woz_uptime_us() is nonzero by the time BLE is up). */
static int64_t s_stamp_us[ALIRO_LAT_PHASE_COUNT];

static const char *const k_phase_name[ALIRO_LAT_PHASE_COUNT] = {
	"connect", "op05", "auth0", "auth1", "exch", "apc", "m4", "range", "trusted", "bolt",
};

void aliro_lat_begin(void)
{
	memset(s_stamp_us, 0, sizeof(s_stamp_us));
	s_stamp_us[ALIRO_LAT_BLE_CONNECT] = woz_uptime_us();
}

void aliro_lat_mark(enum aliro_lat_phase phase)
{
	if ((unsigned)phase >= ALIRO_LAT_PHASE_COUNT || s_stamp_us[phase] != 0) {
		return;
	}
	s_stamp_us[phase] = woz_uptime_us();
}

void aliro_lat_report(void)
{
	int64_t t0 = s_stamp_us[ALIRO_LAT_BLE_CONNECT];

	if (t0 == 0) {
		woz_printf("aliro-lat: no trace (no BLE connect marked)\n");
		return;
	}

	/* One line, every phase as +ms from connect ("-" = never reached). */
	woz_printf("aliro-lat:");
	for (int i = 0; i < ALIRO_LAT_PHASE_COUNT; i++) {
		if (s_stamp_us[i] != 0) {
			woz_printf(" %s+%d", k_phase_name[i], (int)((s_stamp_us[i] - t0) / 1000));
		} else {
			woz_printf(" %s-", k_phase_name[i]);
		}
	}
	woz_printf(" ms\n");
}
