#ifndef _CMD_HAL_UART_RELIABLE_H_
#define _CMD_HAL_UART_RELIABLE_H_

#ifdef CONFIG_CUSTOM_YED

#include <stdint.h>

#include "uart.h"

#define YED_UART_ACK_TIMEOUT_MIN_MS       100U
#define YED_UART_ACK_TIMEOUT_MAX_MS       5000U
#define YED_UART_FAST_RETRY_COUNT_MAX     20U
#define YED_UART_SLOW_RETRY_MIN_MS        200U
#define YED_UART_SLOW_RETRY_MAX_MS        60000U

typedef struct {
	uint32_t ack_timeout_ms;
	uint32_t fast_retry_count;
	uint32_t slow_retry_ms;
} yed_uart_retry_config_t;

typedef struct yed_uart_reliable yed_uart_reliable_t;

typedef void (*yed_uart_rx_replay_fn)(void *arg, const uint8_t *data,
					      unsigned int len);
typedef void (*yed_uart_rx_notify_fn)(void *arg);

yed_uart_reliable_t *yed_uart_reliable_create(E_DRV_UART_NUM uart_num,
					       yed_uart_rx_replay_fn replay_fn,
					       yed_uart_rx_notify_fn notify_fn,
					       void *replay_arg);

int yed_uart_reliable_write(yed_uart_reliable_t *ctx, const uint8_t *data,
				    unsigned int len);

void yed_uart_reliable_rx_byte(yed_uart_reliable_t *ctx, uint8_t byte);

int yed_uart_reliable_get_retry_config(yed_uart_retry_config_t *config);

int yed_uart_reliable_set_retry_config(
				const yed_uart_retry_config_t *config);

#endif

#endif
