/** @file ccc_mac.h — CCC UWB MAC layer: ranging-round scheduling, SP0 frame codec, DS-TWR. */

#ifndef CCC_MAC_H
#define CCC_MAC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ccc_kdf.h"

/** Default hopping round index S(i) in [0, n_round) for a ranging block, keyed by HOP_Key_RW. */
uint16_t ccc_hop_round_index(uint32_t block_index, uint32_t hop_key_rw,
			     uint32_t n_round);

/* ── SP0 frame codec ──────────────────────────────────────────────────────── */

/** SP0 MHR length, bytes. */
#define CCC_MHR_LEN 23
/** Pre-POLL MAC payload length, bytes. */
#define CCC_PRE_POLL_LEN 13
/** Final_Data payload length before the responder records, bytes. */
#define CCC_FINAL_DATA_HDR_LEN 18
/** One responder timestamp record in a Final_Data payload, bytes. */
#define CCC_RESPONDER_LEN 7
/** Max responders in one Final_Data (127-byte payload cap). */
#define CCC_MAX_RESPONDERS 10
/** Vendor Specific Information message ID for Pre-POLL. */
#define CCC_MSG_ID_PRE_POLL 0x01u
/** Vendor Specific Information message ID for Final_Data. */
#define CCC_MSG_ID_FINAL_DATA 0x02u

/** The per-frame-variable fields of an SP0 MHR (fixed fields are built in by ccc_build_mhr). */
struct ccc_mhr_fields {
	uint16_t dest_short_addr;               /**< Destination Short Address. */
	uint32_t frame_counter;                 /**< Aux Security Header frame counter. */
	uint8_t  key_source[CCC_KEYSOURCE_LEN]; /**< Aux Security Header Key Source. */
	uint8_t  msg_id;                        /**< Vendor Info byte 1 (@c CCC_MSG_ID_*). */
	uint8_t  payload_len;                   /**< Vendor Info byte 2 (MAC payload length). */
};

/** Build the 23-byte SP0 MHR (little-endian on the wire). */
int ccc_build_mhr(const struct ccc_mhr_fields *f, uint8_t out[CCC_MHR_LEN]);

/** Parse and validate a 23-byte SP0 MHR, extracting the variable fields (-EINVAL on mismatch). */
int ccc_parse_mhr(const uint8_t in[CCC_MHR_LEN], struct ccc_mhr_fields *f);

/** Pre-POLL request message parameters. */
struct ccc_pre_poll {
	uint32_t uwb_session_id; /**< ID of the UWB ranging session. */
	uint32_t poll_sts_index; /**< STS index of the succeeding POLL message. */
	uint16_t ranging_block;  /**< Session's index of the current ranging block. */
	uint8_t  hop_flag;       /**< Hop flag for the current ranging block. */
	uint16_t round_index;    /**< Ranging round index for the current block. */
};

/** Pack a Pre-POLL payload little-endian. */
int ccc_pre_poll_pack(const struct ccc_pre_poll *p, uint8_t out[CCC_PRE_POLL_LEN]);

/** Parse a 13-byte Pre-POLL payload. */
int ccc_pre_poll_parse(const uint8_t in[CCC_PRE_POLL_LEN], struct ccc_pre_poll *p);

/** One responder's timestamp record in a Final_Data message. */
struct ccc_responder_ts {
	uint8_t  responder_index;       /**< Index of the responder this record refers to. */
	uint32_t timestamp;             /**< POLL→RESPONSE time difference at the initiator. */
	uint8_t  timestamp_uncertainty; /**< Ranging timestamp uncertainty. */
	uint8_t  ranging_status;        /**< Per-responder ranging status. */
};

/** Final_Data message parameters. */
struct ccc_final_data {
	uint32_t uwb_session_id;      /**< ID of the UWB ranging session. */
	uint16_t ranging_block;       /**< Index of the current ranging block. */
	uint8_t  hop_flag;            /**< Hop flag for the next ranging block. */
	uint16_t round_index;         /**< Ranging round for the next ranging block. */
	uint32_t final_sts_index;     /**< STS index of the preceding FINAL message. */
	uint32_t ranging_ts_final_tx; /**< POLL→Final transmit time difference. */
	uint8_t  num_responders;      /**< Responder records that follow (≤ CCC_MAX_RESPONDERS). */
	struct ccc_responder_ts responders[CCC_MAX_RESPONDERS];
};

/** Pack a Final_Data payload little-endian. */
int ccc_final_data_pack(const struct ccc_final_data *f, uint8_t *out, size_t cap,
			size_t *len);

/** Parse a Final_Data payload (-EINVAL if length is inconsistent with num_responders). */
int ccc_final_data_parse(const uint8_t *in, size_t len, struct ccc_final_data *f);

/* ── Ranging schedule ─────────────────────────────────────────────────────── */

/** Hopping mode.  Adaptive (interference-driven) is out of scope. */
enum ccc_hop_mode {
	CCC_HOP_NONE = 0,       /**< No hopping: every block uses ranging round 0. */
	CCC_HOP_CONTINUOUS = 1, /**< Continuous hopping: block i uses round S(i). */
};

/** Slot role within a ranging round. */
enum ccc_slot {
	CCC_SLOT_PRE_POLL,   /**< Slot 0 — SP0 data packet, initiator. */
	CCC_SLOT_POLL,       /**< Slot 1 — SP3 RFRAME, initiator. */
	CCC_SLOT_RESPONSE,   /**< Slots 2..1+N — SP3 RFRAME, responder l. */
	CCC_SLOT_FINAL,      /**< SP3 RFRAME, initiator. */
	CCC_SLOT_FINAL_DATA, /**< SP0 data packet, initiator. */
};

/** Per-session ranging schedule parameters (negotiated in setup). */
struct ccc_ran_params {
	uint32_t sts_index0;        /**< STS_Index0 — ranging round 0 of block 0. */
	uint16_t n_slot_per_round;  /**< N_Slot_per_Round. */
	uint16_t n_round;           /**< N_Round — rounds per block (> 0). */
	uint8_t  n_responder;       /**< N_Responder (1..CCC_MAX_RESPONDERS). */
	uint32_t hop_key_rw;        /**< HOP_Key_RW (continuous hopping). */
	enum ccc_hop_mode hop_mode; /**< Hopping mode. */
};

/** The initiator's next-block hop decision, carried in Final_Data. */
struct ccc_hop_decision {
	uint8_t  hop_flag;    /**< Hop_Flag for the next ranging block. */
	uint16_t round_index; /**< Round_Index for the next ranging block. */
};

/** The ranging round a block uses (block 0 uses round 0; continuous hopping uses S(i) for i>=1). */
uint16_t ccc_block_round(const struct ccc_ran_params *p, uint32_t block);

/** STS index for one slot of a ranging round (uint32 wraps mod 2^32). */
uint32_t ccc_slot_sts_index(const struct ccc_ran_params *p, uint32_t block,
			    uint16_t round, enum ccc_slot slot, uint8_t responder);

/** The initiator's hop decision for the block after block, written into its Final_Data. */
struct ccc_hop_decision ccc_initiator_next_hop(const struct ccc_ran_params *p,
					       uint32_t block);

/* ── Double-sided two-way ranging ─────────────────────────────────────────── */

/** The four DS-TWR intervals, in ranging-timestamp ticks (uint32, wrap mod 2^32). */
struct ccc_ds_twr {
	uint32_t t_round1; /**< Initiator POLL tx → RESPONSE rx (t4 − t1). */
	uint32_t t_reply1; /**< Responder POLL rx → RESPONSE tx (t3 − t2). */
	uint32_t t_round2; /**< Responder RESPONSE tx → FINAL rx (t6 − t3). */
	uint32_t t_reply2; /**< Initiator RESPONSE rx → FINAL tx (t5 − t4). */
};

/** DS-TWR one-way time-of-flight in timestamp ticks (0 if the denominator is 0). */
uint32_t ccc_ds_twr_tof(const struct ccc_ds_twr *t);

/** Assemble the DS-TWR intervals at the responder from a received Final_Data. */
int ccc_responder_ds_twr(const struct ccc_final_data *fd, uint8_t responder,
			 uint32_t t_reply1, uint32_t t_round2,
			 struct ccc_ds_twr *out);

/* ── URSK lifetime ────────────────────────────────────────────────────────── */

/** STS index at which the URSK must be discarded: 2^31 − 1. */
#define CCC_STS_INDEX_MAX 0x7FFFFFFFu

/** Whether the current URSK is exhausted for a ranging block (true once its highest STS index would exceed 2^31-1). */
bool ccc_ursk_exhausted(const struct ccc_ran_params *p, uint32_t block);

#endif /* CCC_MAC_H */
