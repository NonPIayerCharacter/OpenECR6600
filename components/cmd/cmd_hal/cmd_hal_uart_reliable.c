#ifdef CONFIG_CUSTOM_YED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "arch_irq.h"
#include "oshal.h"
#include "os_task_config.h"
#include "uart.h"

#include "cmd_hal_uart_reliable.h"
#include "cmd_hal_uart_reliable_protocol.h"

#define YED_UART_AGGREGATE_MS                  4U
#define YED_UART_DEFAULT_ACK_TIMEOUT_MS        100U
#define YED_UART_ACK_CANDIDATE_TIMEOUT_MS      10U
#define YED_UART_DEFAULT_FAST_RETRY_COUNT      5U
#define YED_UART_DEFAULT_SLOW_RETRY_MS         1000U
#define YED_UART_TX_QUEUE_SIZE                 2048U

#if YED_UART_TX_QUEUE_SIZE < YED_UART_PROTOCOL_MAX_PAYLOAD
#error "YED_UART_TX_QUEUE_SIZE must hold at least one DATA payload"
#endif

#if YED_UART_AGGREGATE_MS < 1
#error "YED_UART_AGGREGATE_MS must be positive"
#endif

#if YED_UART_DEFAULT_ACK_TIMEOUT_MS < YED_UART_ACK_TIMEOUT_MIN_MS || \
	YED_UART_DEFAULT_ACK_TIMEOUT_MS > YED_UART_ACK_TIMEOUT_MAX_MS
#error "YED_UART_DEFAULT_ACK_TIMEOUT_MS must be in the range 100..5000"
#endif

#if YED_UART_DEFAULT_FAST_RETRY_COUNT > YED_UART_FAST_RETRY_COUNT_MAX
#error "YED_UART_DEFAULT_FAST_RETRY_COUNT must be in the range 0..20"
#endif

#if YED_UART_DEFAULT_SLOW_RETRY_MS < YED_UART_SLOW_RETRY_MIN_MS || \
	YED_UART_DEFAULT_SLOW_RETRY_MS > YED_UART_SLOW_RETRY_MAX_MS || \
	YED_UART_DEFAULT_ACK_TIMEOUT_MS > YED_UART_DEFAULT_SLOW_RETRY_MS
#error "YED_UART_DEFAULT_SLOW_RETRY_MS is invalid"
#endif

#if YED_UART_ACK_CANDIDATE_TIMEOUT_MS < 1 || \
	YED_UART_ACK_CANDIDATE_TIMEOUT_MS >= YED_UART_ACK_TIMEOUT_MIN_MS
#error "YED_UART_ACK_CANDIDATE_TIMEOUT_MS is invalid"
#endif

#define YED_UART_TX_TASK_STACK_SIZE  2048
#define YED_UART_TX_TASK_PRIORITY    FHOST_CLI_PRIORITY
#define YED_UART_REPLAY_MAX_SIZE     (YED_UART_ACK_FRAME_SIZE + 1U)
#define YED_UART_FRAME_MAX_SIZE      \
	(YED_UART_HEADER_SIZE + YED_UART_PROTOCOL_MAX_PAYLOAD)

struct yed_uart_reliable {
	E_DRV_UART_NUM uart_num;
	yed_uart_rx_replay_fn replay_fn;
	yed_uart_rx_notify_fn notify_fn;
	void *replay_arg;

	os_mutex_handle_t producer_mutex;
	os_mutex_handle_t queue_mutex;
	os_mutex_handle_t config_mutex;
	os_sem_handle_t event_sem;
	os_sem_handle_t space_sem;
	os_timer_handle_t candidate_timer;

	uint8_t tx_queue[YED_UART_TX_QUEUE_SIZE];
	unsigned int queue_rd;
	unsigned int queue_wr;
	unsigned int queue_count;
	uint32_t queue_first_ms;

	yed_uart_retry_config_t config;

	uint16_t tx_seq;
	uint8_t pending_frame[YED_UART_FRAME_MAX_SIZE];
	unsigned int pending_frame_len;
	yed_uart_retry_config_t pending_config;
	uint32_t fast_retries_done;
	uint32_t ack_deadline_ms;
	bool pending_active;
	bool ack_received;
	bool send_due;
	bool ack_deadline_valid;
	bool slow_retry_mode;

	yed_uart_ack_parser_t ack_parser;
};

static yed_uart_reliable_t *s_yed_uart_reliable;

static uint32_t yed_uart_now_ms(void)
{
	return (uint32_t)os_time_get();
}

static bool yed_uart_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
	return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool yed_uart_flush_expired_candidate(yed_uart_reliable_t *ctx,
					      uint32_t now_ms)
{
	uint8_t replay[YED_UART_ACK_FRAME_SIZE];
	unsigned int flags;
	size_t replay_len;

	flags = system_irq_save();
	replay_len = yed_uart_ack_parser_expire(&ctx->ack_parser, now_ms,
							replay);
	if (replay_len) {
		ctx->replay_fn(ctx->replay_arg, replay, (unsigned int)replay_len);
	}
	system_irq_restore(flags);

	if (replay_len) {
		ctx->notify_fn(ctx->replay_arg);
	}
	return replay_len != 0;
}

static void yed_uart_candidate_timeout(os_timer_handle_t timer)
{
	yed_uart_reliable_t *ctx = os_timer_get_arg(timer);

	yed_uart_flush_expired_candidate(ctx, yed_uart_now_ms());
}

static void yed_uart_queue_copy_in(yed_uart_reliable_t *ctx,
				   const uint8_t *data, unsigned int len)
{
	unsigned int first = YED_UART_TX_QUEUE_SIZE - ctx->queue_wr;

	if (first > len) {
		first = len;
	}

	memcpy(ctx->tx_queue + ctx->queue_wr, data, first);
	if (len > first) {
		memcpy(ctx->tx_queue, data + first, len - first);
	}

	ctx->queue_wr = (ctx->queue_wr + len) % YED_UART_TX_QUEUE_SIZE;
	ctx->queue_count += len;
}

static void yed_uart_queue_copy_out(yed_uart_reliable_t *ctx, uint8_t *data,
				    unsigned int len)
{
	unsigned int first = YED_UART_TX_QUEUE_SIZE - ctx->queue_rd;

	if (first > len) {
		first = len;
	}

	memcpy(data, ctx->tx_queue + ctx->queue_rd, first);
	if (len > first) {
		memcpy(data + first, ctx->tx_queue, len - first);
	}

	ctx->queue_rd = (ctx->queue_rd + len) % YED_UART_TX_QUEUE_SIZE;
	ctx->queue_count -= len;
	if (!ctx->queue_count) {
		ctx->queue_first_ms = 0;
	}
}

int yed_uart_reliable_write(yed_uart_reliable_t *ctx, const uint8_t *data,
				    unsigned int len)
{
	unsigned int offset = 0;

	if (!ctx || (!data && len) || arch_irq_context()) {
		return -1;
	}

	if (!len) {
		return 0;
	}

	if (os_mutex_lock(ctx->producer_mutex, WAIT_FOREVER)) {
		return -1;
	}

	while (offset < len) {
		unsigned int available;
		unsigned int copy_len;
		bool was_empty;

		if (os_mutex_lock(ctx->queue_mutex, WAIT_FOREVER)) {
			os_mutex_unlock(ctx->producer_mutex);
			return -1;
		}

		available = YED_UART_TX_QUEUE_SIZE - ctx->queue_count;
		if (!available) {
			os_mutex_unlock(ctx->queue_mutex);
			if (os_sem_wait(ctx->space_sem, WAIT_FOREVER)) {
				os_mutex_unlock(ctx->producer_mutex);
				return -1;
			}
			continue;
		}

		copy_len = len - offset;
		if (copy_len > available) {
			copy_len = available;
		}

		was_empty = ctx->queue_count == 0;
		yed_uart_queue_copy_in(ctx, data + offset, copy_len);
		if (was_empty) {
			ctx->queue_first_ms = yed_uart_now_ms();
		}
		os_mutex_unlock(ctx->queue_mutex);

		offset += copy_len;
		os_sem_post(ctx->event_sem);
	}

	os_mutex_unlock(ctx->producer_mutex);
	return 0;
}

void yed_uart_reliable_rx_byte(yed_uart_reliable_t *ctx, uint8_t byte)
{
	uint8_t replay[YED_UART_REPLAY_MAX_SIZE];
	uint32_t now_ms;
	unsigned int flags;
	size_t replay_len;
	bool wait_ack;
	bool ack_received;
	bool candidate_pending;
	bool restart_candidate_timer;
	uint16_t pending_seq;
	uint8_t previous_candidate_len;
	uint32_t previous_candidate_deadline;

	if (!ctx) {
		return;
	}

	now_ms = yed_uart_now_ms();
	flags = system_irq_save();
	wait_ack = ctx->pending_active;
	pending_seq = ctx->tx_seq;
	previous_candidate_len = ctx->ack_parser.len;
	previous_candidate_deadline = ctx->ack_parser.deadline_ms;
	replay_len = yed_uart_ack_parser_feed(&ctx->ack_parser, byte, now_ms,
						 YED_UART_ACK_CANDIDATE_TIMEOUT_MS,
						 wait_ack, pending_seq, replay,
						 &ack_received);
	if (ack_received) {
		ctx->ack_received = true;
		ctx->ack_deadline_valid = false;
	}
	candidate_pending = ctx->ack_parser.len != 0;
	restart_candidate_timer = candidate_pending &&
		(!previous_candidate_len ||
		 ctx->ack_parser.deadline_ms != previous_candidate_deadline);
	if (replay_len) {
		ctx->replay_fn(ctx->replay_arg, replay, (unsigned int)replay_len);
	}
	system_irq_restore(flags);

	if (replay_len && !arch_irq_context()) {
		ctx->notify_fn(ctx->replay_arg);
	}
	if (restart_candidate_timer) {
		os_timer_reset(ctx->candidate_timer);
	}
	if (ack_received || candidate_pending) {
		os_sem_post(ctx->event_sem);
	}
}

static bool yed_uart_complete_ack(yed_uart_reliable_t *ctx)
{
	unsigned int flags;
	bool completed = false;

	flags = system_irq_save();
	if (ctx->pending_active && ctx->ack_received) {
		ctx->pending_active = false;
		ctx->ack_received = false;
		ctx->send_due = false;
		ctx->ack_deadline_valid = false;
		ctx->slow_retry_mode = false;
		ctx->tx_seq++;
		completed = true;
	}
	system_irq_restore(flags);

	return completed;
}

static void yed_uart_send_pending(yed_uart_reliable_t *ctx)
{
	uint32_t now_ms;
	unsigned int flags;

	drv_uart_write(ctx->uart_num, (char *)ctx->pending_frame,
		       ctx->pending_frame_len);

	while (!drv_uart_tx_fifo_and_tsr_empty_by_num(ctx->uart_num)) {
		yed_uart_flush_expired_candidate(ctx, yed_uart_now_ms());
		os_msleep(1);
	}

	now_ms = yed_uart_now_ms();
	flags = system_irq_save();
	if (ctx->pending_active && !ctx->ack_received) {
		ctx->ack_deadline_ms = now_ms +
			(ctx->slow_retry_mode ? ctx->pending_config.slow_retry_ms :
			 ctx->pending_config.ack_timeout_ms);
		ctx->ack_deadline_valid = true;
	}
	system_irq_restore(flags);
}

static bool yed_uart_process_pending(yed_uart_reliable_t *ctx,
				     uint32_t now_ms)
{
	unsigned int flags;
	bool send = false;
	bool progressed = false;

	flags = system_irq_save();
	if (ctx->pending_active && ctx->send_due) {
		ctx->send_due = false;
		send = true;
		progressed = true;
	} else if (ctx->pending_active && ctx->ack_deadline_valid &&
		   yed_uart_time_reached(now_ms, ctx->ack_deadline_ms)) {
		ctx->ack_deadline_valid = false;
		progressed = true;

		if (ctx->slow_retry_mode) {
			send = true;
		} else if (ctx->fast_retries_done <
			   ctx->pending_config.fast_retry_count) {
			ctx->fast_retries_done++;
			if (ctx->fast_retries_done ==
			    ctx->pending_config.fast_retry_count) {
				ctx->slow_retry_mode = true;
			}
			send = true;
		} else {
			ctx->slow_retry_mode = true;
			ctx->ack_deadline_ms = now_ms +
					       ctx->pending_config.slow_retry_ms;
			ctx->ack_deadline_valid = true;
		}
	}
	system_irq_restore(flags);

	if (send) {
		yed_uart_send_pending(ctx);
	}

	return progressed;
}

static bool yed_uart_assemble_pending(yed_uart_reliable_t *ctx,
				      uint32_t now_ms)
{
	uint8_t payload[YED_UART_PROTOCOL_MAX_PAYLOAD];
	yed_uart_retry_config_t config;
	uint16_t seq;
	unsigned int payload_len;
	unsigned int frame_len;
	unsigned int flags;
	bool ready;

	flags = system_irq_save();
	ready = !ctx->pending_active;
	system_irq_restore(flags);
	if (!ready) {
		return false;
	}

	if (os_mutex_lock(ctx->config_mutex, WAIT_FOREVER)) {
		return false;
	}
	config = ctx->config;
	os_mutex_unlock(ctx->config_mutex);

	if (os_mutex_lock(ctx->queue_mutex, WAIT_FOREVER)) {
		return false;
	}

	ready = ctx->queue_count >= YED_UART_PROTOCOL_MAX_PAYLOAD ||
		(ctx->queue_count &&
		 now_ms - ctx->queue_first_ms >= YED_UART_AGGREGATE_MS);
	if (!ready) {
		os_mutex_unlock(ctx->queue_mutex);
		return false;
	}

	payload_len = ctx->queue_count;
	if (payload_len > YED_UART_PROTOCOL_MAX_PAYLOAD) {
		payload_len = YED_UART_PROTOCOL_MAX_PAYLOAD;
	}
	yed_uart_queue_copy_out(ctx, payload, payload_len);
	os_mutex_unlock(ctx->queue_mutex);
	os_sem_post(ctx->space_sem);

	flags = system_irq_save();
	seq = ctx->tx_seq;
	system_irq_restore(flags);

	frame_len = (unsigned int)yed_uart_build_data_frame(
		seq, payload, payload_len, ctx->pending_frame,
		sizeof(ctx->pending_frame));
	if (!frame_len) {
		return false;
	}

	flags = system_irq_save();
	ctx->pending_frame_len = frame_len;
	ctx->pending_config = config;
	ctx->fast_retries_done = 0;
	ctx->ack_deadline_ms = 0;
	ctx->pending_active = true;
	ctx->ack_received = false;
	ctx->send_due = true;
	ctx->ack_deadline_valid = false;
	ctx->slow_retry_mode = false;
	system_irq_restore(flags);

	return true;
}

static void yed_uart_reduce_wait(uint32_t now_ms, uint32_t deadline_ms,
				 unsigned int *wait_ms)
{
	int32_t remaining;

	remaining = (int32_t)(deadline_ms - now_ms);
	if (remaining <= 0) {
		*wait_ms = 0;
		return;
	}

	if ((uint32_t)remaining < *wait_ms) {
		*wait_ms = (unsigned int)remaining;
	}
}

static unsigned int yed_uart_next_wait(yed_uart_reliable_t *ctx,
				       uint32_t now_ms)
{
	unsigned int wait_ms = WAIT_FOREVER;
	unsigned int flags;
	bool pending_active;
	bool immediate;
	bool deadline_valid;
	uint32_t deadline_ms;

	flags = system_irq_save();
	pending_active = ctx->pending_active;
	immediate = ctx->ack_received || ctx->send_due;
	deadline_valid = ctx->ack_deadline_valid;
	deadline_ms = ctx->ack_deadline_ms;
	if (ctx->ack_parser.len) {
		yed_uart_reduce_wait(now_ms, ctx->ack_parser.deadline_ms, &wait_ms);
	}
	system_irq_restore(flags);

	if (immediate) {
		return 0;
	}
	if (pending_active && deadline_valid) {
		yed_uart_reduce_wait(now_ms, deadline_ms, &wait_ms);
	}

	if (!pending_active && !os_mutex_lock(ctx->queue_mutex, WAIT_FOREVER)) {
		if (ctx->queue_count >= YED_UART_PROTOCOL_MAX_PAYLOAD) {
			wait_ms = 0;
		} else if (ctx->queue_count) {
			yed_uart_reduce_wait(now_ms,
				ctx->queue_first_ms + YED_UART_AGGREGATE_MS,
				&wait_ms);
		}
		os_mutex_unlock(ctx->queue_mutex);
	}

	return wait_ms;
}

static void yed_uart_tx_task(void *arg)
{
	yed_uart_reliable_t *ctx = arg;

	while (1) {
		bool progressed;

		do {
			uint32_t now_ms = yed_uart_now_ms();

			progressed = yed_uart_flush_expired_candidate(ctx, now_ms);
			if (yed_uart_complete_ack(ctx)) {
				progressed = true;
				continue;
			}
			if (yed_uart_process_pending(ctx, now_ms)) {
				progressed = true;
				continue;
			}
			if (yed_uart_assemble_pending(ctx, now_ms)) {
				progressed = true;
			}
		} while (progressed);

		os_sem_wait(ctx->event_sem,
			    yed_uart_next_wait(ctx, yed_uart_now_ms()));
	}
}

static void yed_uart_reliable_free(yed_uart_reliable_t *ctx)
{
	if (ctx->candidate_timer) {
		os_timer_delete(ctx->candidate_timer);
	}
	if (ctx->space_sem) {
		os_sem_destroy(ctx->space_sem);
	}
	if (ctx->event_sem) {
		os_sem_destroy(ctx->event_sem);
	}
	if (ctx->config_mutex) {
		os_mutex_destroy(ctx->config_mutex);
	}
	if (ctx->queue_mutex) {
		os_mutex_destroy(ctx->queue_mutex);
	}
	if (ctx->producer_mutex) {
		os_mutex_destroy(ctx->producer_mutex);
	}
	os_free(ctx);
}

yed_uart_reliable_t *yed_uart_reliable_create(E_DRV_UART_NUM uart_num,
					       yed_uart_rx_replay_fn replay_fn,
					       yed_uart_rx_notify_fn notify_fn,
					       void *replay_arg)
{
	yed_uart_reliable_t *ctx;
	int task_handle;

	if (uart_num != E_UART_NUM_1 || !replay_fn || !notify_fn ||
	    s_yed_uart_reliable) {
		return NULL;
	}

	ctx = os_zalloc(sizeof(*ctx));
	if (!ctx) {
		return NULL;
	}

	ctx->uart_num = uart_num;
	ctx->replay_fn = replay_fn;
	ctx->notify_fn = notify_fn;
	ctx->replay_arg = replay_arg;
	ctx->config.ack_timeout_ms = YED_UART_DEFAULT_ACK_TIMEOUT_MS;
	ctx->config.fast_retry_count = YED_UART_DEFAULT_FAST_RETRY_COUNT;
	ctx->config.slow_retry_ms = YED_UART_DEFAULT_SLOW_RETRY_MS;
	yed_uart_ack_parser_init(&ctx->ack_parser);

	ctx->producer_mutex = os_mutex_create();
	ctx->queue_mutex = os_mutex_create();
	ctx->config_mutex = os_mutex_create();
	ctx->event_sem = os_sem_create(1, 0);
	ctx->space_sem = os_sem_create(1, 0);
	ctx->candidate_timer = os_timer_create("yed-ack-candidate",
		YED_UART_ACK_CANDIDATE_TIMEOUT_MS, 0,
		yed_uart_candidate_timeout, ctx);
	if (!ctx->producer_mutex || !ctx->queue_mutex || !ctx->config_mutex ||
	    !ctx->event_sem || !ctx->space_sem || !ctx->candidate_timer) {
		yed_uart_reliable_free(ctx);
		return NULL;
	}

	s_yed_uart_reliable = ctx;
	task_handle = os_task_create("yed-uart-tx", YED_UART_TX_TASK_PRIORITY,
				     YED_UART_TX_TASK_STACK_SIZE,
				     yed_uart_tx_task, ctx);
	if (task_handle < 0) {
		s_yed_uart_reliable = NULL;
		yed_uart_reliable_free(ctx);
		return NULL;
	}

	return ctx;
}

int yed_uart_reliable_get_retry_config(yed_uart_retry_config_t *config)
{
	yed_uart_reliable_t *ctx = s_yed_uart_reliable;

	if (!ctx || !config || os_mutex_lock(ctx->config_mutex, WAIT_FOREVER)) {
		return -1;
	}

	*config = ctx->config;
	os_mutex_unlock(ctx->config_mutex);
	return 0;
}

int yed_uart_reliable_set_retry_config(
				const yed_uart_retry_config_t *config)
{
	yed_uart_reliable_t *ctx = s_yed_uart_reliable;

	if (!ctx || !config ||
	    config->ack_timeout_ms < YED_UART_ACK_TIMEOUT_MIN_MS ||
	    config->ack_timeout_ms > YED_UART_ACK_TIMEOUT_MAX_MS ||
	    config->fast_retry_count > YED_UART_FAST_RETRY_COUNT_MAX ||
	    config->slow_retry_ms < YED_UART_SLOW_RETRY_MIN_MS ||
	    config->slow_retry_ms > YED_UART_SLOW_RETRY_MAX_MS ||
	    config->ack_timeout_ms > config->slow_retry_ms) {
		return -1;
	}

	if (os_mutex_lock(ctx->config_mutex, WAIT_FOREVER)) {
		return -1;
	}
	ctx->config = *config;
	os_mutex_unlock(ctx->config_mutex);
	return 0;
}

#endif
