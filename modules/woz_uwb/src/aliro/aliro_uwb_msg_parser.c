/** @file aliro_uwb_msg_parser.c — TLV attribute parser and big-endian reads. */

#include "aliro_uwb_msg_parser.h"

#include "woz_log.h"

LOG_MODULE_DECLARE(woz_aliro_uwb, LOG_LEVEL_INF);

struct aliro_uwb_msg_attribute
	*
	/**
	 * @brief Parses the next TLV attribute from the message payload; returns NULL if offset
	 * exceeds declared message length, clamping to prevent overrun.
	 */
	aliro_uwb_msg_next_attribute(struct aliro_uwb_msg_parser *parser)
{
	struct aliro_uwb_msg_attribute *attr;

	/* Need the full 2-byte attribute header (id + length) present before
	 * dereferencing attr->length; a lone trailing byte would over-read
	 * parser->data[offset + 1]. Written as an addition (offset <= length is an
	 * invariant here) to avoid a size_t underflow. */
	if (parser->offset + ALIRO_ATTRIBUTE_HEADER_LENGTH > parser->length) {
		return NULL;
	}

	attr = (struct aliro_uwb_msg_attribute *)&parser->data[parser->offset];
	parser->offset += ALIRO_ATTRIBUTE_HEADER_LENGTH + attr->length;

	/* Clamp and stop on a declared length that overruns the payload. */
	if (parser->offset > parser->length) {
		parser->offset = parser->length;
		return NULL;
	}

	return attr;
}

/**
 * @brief Decodes a big-endian fixed-width integer from an attribute; returns false if declared
 * length does not match width or on parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param width Expected byte width of the encoded integer.
 * @param out Destination for the decoded value.
 * @return true on success, false if the attribute's declared length does not match width.
 */
static bool read_be(const struct aliro_uwb_msg_attribute *attr, const char *name, uint8_t width,
		    uint64_t *out)
{
	uint64_t value = 0;
	uint8_t i;

	if (attr->length != width) {
		LOG_ERR("attr %s: length %u, expected %u", name, attr->length, width);
		return false;
	}

	for (i = 0; i < width; i++) {
		value = (value << 8) | attr->value[i];
	}
	*out = value;
	return true;
}

/**
 * @brief Decodes an 8-bit big-endian integer from an attribute; returns false on width mismatch or
 * parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param out Destination for the decoded value.
 * @return true on success, false on a size mismatch.
 */
bool aliro_uwb_msg_read_u8(const struct aliro_uwb_msg_attribute *attr, const char *name,
			   uint8_t *out)
{
	uint64_t value;

	if (!read_be(attr, name, sizeof(uint8_t), &value)) {
		return false;
	}
	*out = (uint8_t)value;
	return true;
}

/**
 * @brief Decodes a 16-bit big-endian integer from an attribute; returns false on width mismatch or
 * parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param out Destination for the decoded value.
 * @return true on success, false on a size mismatch.
 */
bool aliro_uwb_msg_read_u16(const struct aliro_uwb_msg_attribute *attr, const char *name,
			    uint16_t *out)
{
	uint64_t value;

	if (!read_be(attr, name, sizeof(uint16_t), &value)) {
		return false;
	}
	*out = (uint16_t)value;
	return true;
}

/**
 * @brief Decodes a 32-bit big-endian integer from an attribute; returns false on width mismatch or
 * parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param out Destination for the decoded value.
 * @return true on success, false on a size mismatch.
 */
bool aliro_uwb_msg_read_u32(const struct aliro_uwb_msg_attribute *attr, const char *name,
			    uint32_t *out)
{
	uint64_t value;

	if (!read_be(attr, name, sizeof(uint32_t), &value)) {
		return false;
	}
	*out = (uint32_t)value;
	return true;
}

/**
 * @brief Decodes a 64-bit big-endian integer from an attribute; returns false on width mismatch or
 * parse error.
 * @param attr Attribute whose value bytes are decoded.
 * @param name Attribute name, used for error logging.
 * @param out Destination for the decoded value.
 * @return true on success, false on a size mismatch.
 */
bool aliro_uwb_msg_read_u64(const struct aliro_uwb_msg_attribute *attr, const char *name,
			    uint64_t *out)
{
	return read_be(attr, name, sizeof(uint64_t), out);
}
