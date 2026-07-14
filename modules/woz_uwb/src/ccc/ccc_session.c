/** @file ccc_session.c — Aliro/CCC ranging seam implementation. See ccc_session.h. */

#include "ccc_session.h"

#include <errno.h>

uint16_t ccc_session_n_round(const struct ccc_ran_session *s)
{
	uint32_t denom;

	if (s == NULL) {
		return 0u;
	}
	denom = (uint32_t)s->n_chap_per_slot * s->n_slot_per_round;
	if (denom == 0u) {
		return 0u;
	}
	/* N_Round for the ranging block. */
	return (uint16_t)((288u * (uint32_t)s->n_ran_s) / denom);
}

int ccc_session_to_ran_params(const struct ccc_ran_session *s,
			      // RAN (Random Access Number) parameters: multiplier, index, and preamble code, populated by CCC during session setup.
			      struct ccc_ran_params *out)
{
	uint16_t n_round;

	if (s == NULL || out == NULL) {
		return -EINVAL;
	}
	/* A round must hold the N_Responder + 4 packets (Pre-POLL, POLL, Final,
	 * Final_Data + N responses). */
	if ((uint16_t)s->n_slot_per_round < (uint16_t)s->n_responder + 4u) {
		return -EINVAL;
	}
	n_round = ccc_session_n_round(s);
	if (n_round == 0u) {
		return -EINVAL;
	}

	out->sts_index0 = s->sts_index0;
	out->n_slot_per_round = s->n_slot_per_round;
	out->n_round = n_round;
	out->n_responder = s->n_responder;
	out->hop_key_rw = s->hop_key_rw;
	out->hop_mode = s->hop_mode;
	return 0;
}
