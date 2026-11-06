#ifndef _CMD_HAL_UART_RELIABLE_PROTOCOL_H_
#define _CMD_HAL_UART_RELIABLE_PROTOCOL_H_

#ifdef CONFIG_CUSTOM_YED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define YED_UART_MAGIC_SIZE             4U
#define YED_UART_HEADER_CRC_INPUT_SIZE  8U
#define YED_UART_HEADER_SIZE            10U
#define YED_UART_ACK_FRAME_SIZE         YED_UART_HEADER_SIZE
#define YED_UART_PROTOCOL_MAX_PAYLOAD   200U

typedef struct {
	uint8_t data[YED_UART_ACK_FRAME_SIZE];
	uint8_t len;
	uint32_t deadline_ms;
} yed_uart_ack_parser_t;

size_t yed_uart_build_data_frame(uint16_t seq, const uint8_t *data,
					 size_t len, uint8_t *frame,
					 size_t frame_size);

void yed_uart_ack_parser_init(yed_uart_ack_parser_t *parser);

size_t yed_uart_ack_parser_feed(yed_uart_ack_parser_t *parser,
					uint8_t byte, uint32_t now_ms,
					uint32_t candidate_timeout_ms,
					bool wait_ack, uint16_t pending_seq,
					uint8_t *replay, bool *ack_received);

size_t yed_uart_ack_parser_expire(yed_uart_ack_parser_t *parser,
					  uint32_t now_ms, uint8_t *replay);

#endif

#endif
