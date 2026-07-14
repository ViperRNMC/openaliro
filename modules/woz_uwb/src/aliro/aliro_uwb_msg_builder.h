/** @file aliro_uwb_msg_builder.h — big-endian TLV message builder. */

#pragma once

#include "aliro_uwb_msg_spec.h"

#include <aliro_uwb_adapter/aliro_uwb_session.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Accumulates bytes into a heap-allocated message. */
struct aliro_uwb_msg_builder {
	/**
	 * @brief Aliro UWB message under construction, holding encoded M1-M4 attributes and payload.
	 */
	struct aliro_uwb_message *message;
	size_t capacity;
};

/** Allocate a message with room for @payload_len payload bytes plus header. */
bool aliro_uwb_msg_builder_init(struct aliro_uwb_msg_builder *builder,
				uint16_t payload_len);

/** Append the 4-byte header (protocol, id, big-endian payload length). */
void aliro_uwb_msg_builder_header(struct aliro_uwb_msg_builder *builder,
				  uint8_t protocol, uint8_t id,
				  uint16_t payload_length);

/** Append a 1-byte-value attribute. */
bool aliro_uwb_msg_builder_add_u8(struct aliro_uwb_msg_builder *builder,
				  uint8_t id, uint8_t value);

/** Append a 2-byte big-endian attribute. */
bool aliro_uwb_msg_builder_add_u16(struct aliro_uwb_msg_builder *builder,
				   uint8_t id, uint16_t value);

/** Append a 4-byte big-endian attribute. */
bool aliro_uwb_msg_builder_add_u32(struct aliro_uwb_msg_builder *builder,
				   uint8_t id, uint32_t value);

/** Append a 8-byte big-endian attribute. */
bool aliro_uwb_msg_builder_add_u64(struct aliro_uwb_msg_builder *builder,
				   uint8_t id, uint64_t value);

/** Append an attribute whose value is @count big-endian 16-bit words. */
bool aliro_uwb_msg_builder_add_u16_array(struct aliro_uwb_msg_builder *builder,
					 uint8_t id, size_t count,
					 const uint16_t *values);

/**
 * @brief Append an attribute whose value is @count raw bytes.
 */
bool aliro_uwb_msg_builder_add_bytes(struct aliro_uwb_msg_builder *builder,
				     uint8_t id, size_t count,
				     const uint8_t *values);
