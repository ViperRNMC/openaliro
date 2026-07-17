/** @file aliro_round_config.h — one knob for the CCC ranging round's responder count. */

#ifndef ALIRO_ROUND_CONFIG_H
#define ALIRO_ROUND_CONFIG_H

/*
 * EXPERIMENT-2RESP: the responder count appears in two places that MUST agree —
 * the M3 NUMBER_RESPONDERS_NODES attribute (aliro_uwb_msg.c) and rcfg[12]
 * Number_Responder_Nodes (cherry_ccc_shim.c). Both feed the RangingConfiguration
 * SaltedHash the Wallet independently recomputes; if they disagree, every derived
 * STS/dURSK/dUDSK diverges and nothing decodes. Defining the count once here makes
 * that invariant compiler-enforced instead of two literals a future edit can desync.
 *
 * The reader's Final-RFRAME RX offset (ccc_shim_rx.c) is derived from the same
 * knob: the phone puts its Final one slot after the last responder, so the Final
 * moves out by one slot for each extra responder.
 *
 *   ALIRO_NUM_RESPONDERS 1 = validated 1:1 baseline (default; normal firmware).
 *   ALIRO_NUM_RESPONDERS 2 = dual-anchor round; needs a real second responder that
 *                            transmits Response_1 at POLL+2 slots / STS index+2.
 */
#ifndef ALIRO_NUM_RESPONDERS
#define ALIRO_NUM_RESPONDERS 1u
#endif

/*
 * Slot offset of the phone's Final RFRAME from the POLL, in ranging slots and,
 * equivalently, in STS index steps: slot_offset(FINAL) relative to POLL = N + 1
 * (responder l replies at POLL+1+l; the Final sits one slot past the last one).
 * n=1 -> POLL+2, n=2 -> POLL+3.
 */
#define ALIRO_FINAL_SLOT_OFFSET (ALIRO_NUM_RESPONDERS + 1u)

#endif /* ALIRO_ROUND_CONFIG_H */
