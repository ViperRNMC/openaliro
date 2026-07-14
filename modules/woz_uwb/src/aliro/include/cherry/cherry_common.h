/** @file cherry_common.h — diagnostics config struct and report forward decl. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-session diagnostics selection flags and CIR window parameters, passed by value.
 */
struct cherry_common_diag_cfg {
	bool extra_status;
	bool aoa;
	bool cfo;
	bool emitter_addr;
	bool seg_metrics;
	bool cirs;
	bool timestamp;
	bool rssi;
	bool rx_path;
	bool rx_debugging;
	bool fira_msg_id;
	uint8_t cir_window_size;
	uint8_t cir_window_fp_offset;
};

/**
 * @brief Opaque diagnostics report structure, referenced only by pointer in event unions.
 */
struct cherry_common_diag_report;

#ifdef __cplusplus
}
#endif
