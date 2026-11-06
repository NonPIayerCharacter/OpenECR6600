#ifdef CONFIG_CUSTOM_YED

#include <string.h>

#include "cmd_hal_uart_reliable_protocol.h"

static const uint8_t s_yed_data_magic[YED_UART_MAGIC_SIZE] = {
	0xA5, 0x5A, 0xC3, 0x3C
};

static const uint8_t s_yed_ack_magic[YED_UART_MAGIC_SIZE] = {
	0x5A, 0xA5, 0x3C, 0xC3
};

static void yed_uart_put_le16(uint8_t *dst, uint16_t value)
{
	dst[0] = (uint8_t)value;
	dst[1] = (uint8_t)(value >> 8);
}

static uint16_t yed_uart_get_le16(const uint8_t *src)
{
	return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static bool yed_uart_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
	return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint16_t yed_uart_crc16_modbus(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;
	size_t i;

	for (i = 0; i < len; i++) {
		uint8_t bit;

		crc ^= data[i];
		for (bit = 0; bit < 8; bit++) {
			if (crc & 1U) {
				crc = (crc >> 1) ^ 0xA001;
			} else {
				crc >>= 1;
			}
		}
	}

	return crc;
}

size_t yed_uart_build_data_frame(uint16_t seq, const uint8_t *data,
					 size_t len, uint8_t *frame,
					 size_t frame_size)
{
	uint16_t crc;

	if (!data || !frame || !len || len > YED_UART_PROTOCOL_MAX_PAYLOAD ||
	    frame_size < YED_UART_HEADER_SIZE + len) {
		return 0;
	}

	memcpy(frame, s_yed_data_magic, YED_UART_MAGIC_SIZE);
	yed_uart_put_le16(frame + 4, seq);
	yed_uart_put_le16(frame + 6, (uint16_t)len);
	crc = yed_uart_crc16_modbus(frame, YED_UART_HEADER_CRC_INPUT_SIZE);
	yed_uart_put_le16(frame + 8, crc);
	memcpy(frame + YED_UART_HEADER_SIZE, data, len);

	return YED_UART_HEADER_SIZE + len;
}

void yed_uart_ack_parser_init(yed_uart_ack_parser_t *parser)
{
	memset(parser, 0, sizeof(*parser));
}

static size_t yed_uart_ack_parser_reject(yed_uart_ack_parser_t *parser,
						 uint32_t now_ms,
						 uint32_t candidate_timeout_ms,
						 uint8_t *replay)
{
	size_t keep;
	size_t replay_len;

	keep = parser->len - 1U;
	if (keep > YED_UART_MAGIC_SIZE) {
		keep = YED_UART_MAGIC_SIZE;
	}

	while (keep && memcmp(parser->data + parser->len - keep,
				      s_yed_ack_magic, keep) != 0) {
		keep--;
	}

	replay_len = parser->len - keep;
	memcpy(replay, parser->data, replay_len);

	if (keep) {
		memmove(parser->data, parser->data + replay_len, keep);
		parser->len = (uint8_t)keep;
		parser->deadline_ms = now_ms + candidate_timeout_ms;
	} else {
		parser->len = 0;
		parser->deadline_ms = 0;
	}

	return replay_len;
}

size_t yed_uart_ack_parser_expire(yed_uart_ack_parser_t *parser,
					  uint32_t now_ms, uint8_t *replay)
{
	size_t replay_len;

	if (!parser->len ||
	    !yed_uart_time_reached(now_ms, parser->deadline_ms)) {
		return 0;
	}

	replay_len = parser->len;
	memcpy(replay, parser->data, replay_len);
	parser->len = 0;
	parser->deadline_ms = 0;
	return replay_len;
}

size_t yed_uart_ack_parser_feed(yed_uart_ack_parser_t *parser,
					uint8_t byte, uint32_t now_ms,
					uint32_t candidate_timeout_ms,
					bool wait_ack, uint16_t pending_seq,
					uint8_t *replay, bool *ack_received)
{
	size_t replay_len = 0;
	uint16_t crc;

	*ack_received = false;
	replay_len = yed_uart_ack_parser_expire(parser, now_ms, replay);

	if (!parser->len) {
		if (byte != s_yed_ack_magic[0]) {
			replay[replay_len++] = byte;
			return replay_len;
		}

		parser->data[0] = byte;
		parser->len = 1;
		parser->deadline_ms = now_ms + candidate_timeout_ms;
		return replay_len;
	}

	parser->data[parser->len++] = byte;

	if (parser->len <= YED_UART_MAGIC_SIZE &&
	    memcmp(parser->data, s_yed_ack_magic, parser->len) != 0) {
		return replay_len + yed_uart_ack_parser_reject(parser, now_ms,
								 candidate_timeout_ms,
								 replay + replay_len);
	}

	if (parser->len < YED_UART_ACK_FRAME_SIZE) {
		return replay_len;
	}

	crc = yed_uart_crc16_modbus(parser->data,
				    YED_UART_HEADER_CRC_INPUT_SIZE);
	if (wait_ack && yed_uart_get_le16(parser->data + 4) == pending_seq &&
	    yed_uart_get_le16(parser->data + 6) == 0 &&
	    yed_uart_get_le16(parser->data + 8) == crc) {
		parser->len = 0;
		parser->deadline_ms = 0;
		*ack_received = true;
		return replay_len;
	}

	return replay_len + yed_uart_ack_parser_reject(parser, now_ms,
							 candidate_timeout_ms,
							 replay + replay_len);
}

#endif
