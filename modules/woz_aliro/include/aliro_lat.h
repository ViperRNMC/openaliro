/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_lat — walk-up latency trace. One timestamp per phase boundary of the
 * BLE connect -> credential auth -> UWB ranging -> bolt pipeline, stamped at
 * first occurrence and printed as a single consolidated budget line so a bench
 * trace ranks the levers before anything is optimized. Diagnostics only:
 * nothing reads these marks on the protocol path.
 */
#ifndef ALIRO_LAT_H
#define ALIRO_LAT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Phase boundaries, in pipeline order. Deltas in the budget line are relative
 * to ALIRO_LAT_BLE_CONNECT (the walk-up's t=0). */
enum aliro_lat_phase {
	ALIRO_LAT_BLE_CONNECT = 0, /* GAP connect on the reader's advertisement */
	ALIRO_LAT_OP05_RX,         /* phone's Initiate-Access-Protocol received */
	ALIRO_LAT_AUTH0_TX,        /* AUTH0 command sent */
	ALIRO_LAT_AUTH1_DONE,      /* AUTH1Response verified; EXCHANGE sent */
	ALIRO_LAT_EXCHANGE_DONE,   /* EXCHANGE response accepted (URSK armed) */
	ALIRO_LAT_AP_COMPLETED,    /* Reader-Status-AP-Completed sent */
	ALIRO_LAT_M4_DONE,         /* UWB session ACTIVE (M4 handled, responder up) */
	ALIRO_LAT_FIRST_RANGE,     /* first DS-TWR range latched */
	ALIRO_LAT_TRUSTED_RANGE,   /* layer-4 consensus reached */
	ALIRO_LAT_BOLT_DRIVEN,     /* unlock dispatched to the lock manager */
	ALIRO_LAT_PHASE_COUNT
};

/* Start a fresh walk-up trace: clear every mark and stamp BLE_CONNECT. */
void aliro_lat_begin(void);

/* Stamp a phase at its first occurrence this walk-up; later calls are no-ops.
 * Cheap (one uptime read + store) — safe on the BLE-host and UWB RX paths. */
void aliro_lat_mark(enum aliro_lat_phase phase);

/* Print the consolidated budget line (one printf; call off the protocol path). */
void aliro_lat_report(void);

#ifdef __cplusplus
}
#endif

#endif /* ALIRO_LAT_H */
