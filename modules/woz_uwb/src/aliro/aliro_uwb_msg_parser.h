/** @file aliro_uwb_msg_parser.h — TLV attribute iteration and big-endian reads. */

#pragma once

#include "aliro_uwb_msg_spec.h"

#include <aliro_uwb_adapter/aliro_uwb_session.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** A single type/length/value attribute overlaid on the message bytes. */
struct aliro_uwb_msg_attribute {
	uint8_t id;
	uint8_t length;
	uint8_t value[];
};

/** Cursor walking the attributes of one message payload. */
struct aliro_uwb_msg_parser {
	size_t length;
	size_t offset;
	const uint8_t *data;
};

/** Initialize a parser positioned just past the 4-byte header. */
#define ALIRO_UWB_MSG_PARSER_INIT(msg_)                                                            \
	{                                                                                          \
		.length = (msg_)->len,                                                             \
		.offset = ALIRO_HEADER_LENGTH,                                                     \
		.data = (msg_)->data,                                                              \
	}

/** Advance to the next attribute; NULL at end-of-payload or on overrun. */
struct aliro_uwb_msg_attribute
	*
	/**
	 * @brief Cursor walking the attributes of one message payload.
	 * @param parser Parser cursor to advance to the next attribute.
	 */
	aliro_uwb_msg_next_attribute(struct aliro_uwb_msg_parser *parser);

/* Exact-length big-endian readers; return false on a size mismatch. */
bool aliro_uwb_msg_read_u8(const struct aliro_uwb_msg_attribute *attr, const char *name,
			   uint8_t *out);
bool aliro_uwb_msg_read_u16(const struct aliro_uwb_msg_attribute *attr, const char *name,
			    uint16_t *out);
bool aliro_uwb_msg_read_u32(const struct aliro_uwb_msg_attribute *attr, const char *name,
			    uint32_t *out);
/**
 * @brief A single type/length/value attribute overlaid on the message bytes.
 * @param attr Attribute to read as a 64-bit value.
 */
bool aliro_uwb_msg_read_u64(const struct aliro_uwb_msg_attribute *attr, const char *name,
			    uint64_t *out);
