#include "ci1302.h"

#include <string.h>

#include "ci1302_protocol.h"
#include "ci1302_uart.h"
#include "oshal.h"

#ifndef CONFIG_CI1302_UART_BAUDRATE
#define CONFIG_CI1302_UART_BAUDRATE 921600
#endif

#define CI1302_RX_CHUNK_SIZE         512U
#define CI1302_RX_TASK_STACK_SIZE    (16U * 1024U)
#define CI1302_TX_TIMEOUT_MS         1000U
#define CI1302_TONE_SAMPLE_RATE      16000U
#define CI1302_TONE_MAX_DURATION_MS  60000U
#define CI1302_RAW_TRACE_LINE_BYTES  16U

typedef struct {
    ci1302_protocol_parser_t parser;
    os_mutex_handle_t mutex;
    os_mutex_handle_t play_mutex;
    int rx_task;
    bool initialized;
    bool running;
    bool ready;
    bool recording;
    bool playing;
    bool streaming;
    uint32_t pending_request;
    bool trace_enabled;
    bool raw_trace_enabled;
    uint8_t volume;
    ci1302_event_callback_t callback;
    void *callback_user_data;
    ci1302_pcm_source_t pcm_source;
    void *pcm_source_user_data;
    uint8_t rx_buffer[CI1302_RX_CHUNK_SIZE];
    ci1302_stats_t stats;
    uint32_t tone_samples_remaining;
    uint8_t tone_sample_index;
} ci1302_context_t;

static ci1302_context_t ci1302_ctx;
static ci1302_event_callback_t platform_callback;
static void *platform_user_data;
static uint8_t ci1302_tx_buffer[CI1302_PROTOCOL_MAX_FRAME_SIZE]
    __attribute__((section(".dma.data"), aligned(4)));
static uint8_t ci1302_pcm_buffer[CI1302_PROTOCOL_MAX_PAYLOAD]
    __attribute__((section(".dma.data"), aligned(4)));

static const int16_t ci1302_tone_1khz[16] = {
     0,  4592,  8485, 11087, 12000, 11087,  8485,  4592,
     0, -4592, -8485,-11087,-12000,-11087, -8485, -4592,
};

static void ci1302_trace_raw_rx(const uint8_t *data, size_t length)
{
    static const char hex[] = "0123456789abcdef";
    char line[CI1302_RAW_TRACE_LINE_BYTES * 3U + 1U];
    size_t offset = 0;

    os_printf(LM_CMD, LL_INFO, "CI1302 RAW RX chunk len=%lu\r\n",
              (unsigned long)length);
    while (offset < length) {
        size_t count = length - offset;
        size_t index;
        size_t position = 0;

        if (count > CI1302_RAW_TRACE_LINE_BYTES) {
            count = CI1302_RAW_TRACE_LINE_BYTES;
        }
        for (index = 0; index < count; ++index) {
            uint8_t byte = data[offset + index];

            line[position++] = hex[byte >> 4];
            line[position++] = hex[byte & 0x0FU];
            line[position++] = ' ';
        }
        line[position] = '\0';
        os_printf(LM_CMD, LL_INFO, "CI1302 RAW RX +%04lx: %s\r\n",
                  (unsigned long)offset, line);
        offset += count;
    }
}

static void ci1302_emit_event(ci1302_event_t event, const uint8_t *data,
                              uint16_t length)
{
    if (platform_callback) {
        platform_callback(event, data, length, platform_user_data);
    }
    if (ci1302_ctx.callback) {
        ci1302_ctx.callback(event, data, length,
                            ci1302_ctx.callback_user_data);
    }
}

int ci1302_send_command(uint16_t command, uint32_t fill_data,
                        const void *payload, uint16_t payload_length)
{
    int frame_length;
    int ret;

    if (!ci1302_ctx.initialized || !ci1302_ctx.mutex) {
        return -1;
    }
    if (os_mutex_lock(ci1302_ctx.mutex, CI1302_TX_TIMEOUT_MS) != 0) {
        return -1;
    }
    frame_length = ci1302_protocol_encode(command, payload, payload_length,
                                           fill_data,
                                           ci1302_tx_buffer,
                                           sizeof(ci1302_tx_buffer));
    if (frame_length < 0) {
        os_mutex_unlock(ci1302_ctx.mutex);
        return -1;
    }
    if (ci1302_ctx.trace_enabled) {
        os_printf(LM_CMD, LL_INFO,
                  "CI1302 TX cmd=0x%04x len=%u fill=0x%08lx\r\n",
                  command, payload_length, (unsigned long)fill_data);
    }
    ret = ci1302_uart_write(ci1302_tx_buffer, (unsigned int)frame_length);
    if (ret == 0) {
        ci1302_ctx.stats.uart_tx_bytes += (uint32_t)frame_length;
    }
    os_mutex_unlock(ci1302_ctx.mutex);
    return ret;
}

static int ci1302_send_frame(uint16_t command, const void *payload,
                             uint16_t payload_length)
{
    return ci1302_send_command(command, CI1302_PROTOCOL_FILL_DEFAULT,
                               payload, payload_length);
}

static int ci1302_tone_source(uint8_t *buffer, uint32_t max_length,
                              void *user_data)
{
    ci1302_context_t *ctx = (ci1302_context_t *)user_data;
    uint32_t max_samples = max_length / sizeof(int16_t);
    uint32_t samples = ctx->tone_samples_remaining < max_samples ?
                       ctx->tone_samples_remaining : max_samples;
    uint32_t index;

    for (index = 0; index < samples; ++index) {
        int16_t sample = ci1302_tone_1khz[ctx->tone_sample_index];
        buffer[index * 2U] = (uint8_t)sample;
        buffer[index * 2U + 1U] = (uint8_t)((uint16_t)sample >> 8);
        ctx->tone_sample_index = (uint8_t)((ctx->tone_sample_index + 1U) & 0x0FU);
    }
    ctx->tone_samples_remaining -= samples;
    return (int)(samples * sizeof(int16_t));
}

static void ci1302_finish_playback(void)
{
    if (!ci1302_ctx.streaming) ci1302_ctx.playing = false;
    ci1302_ctx.pending_request = 0;
    ci1302_ctx.pcm_source = NULL;
    ci1302_ctx.pcm_source_user_data = NULL;
    if (ci1302_send_frame(CI1302_CMD_PLAY_DATA_END, NULL, 0) != 0 && ci1302_ctx.streaming) {
        ci1302_ctx.playing = false;
        ci1302_emit_event(CI1302_EVENT_ERROR, NULL, 0);
    }
}

static void ci1302_serve_playback_locked(const uint8_t *payload,
                                           uint16_t payload_length)
{
    uint32_t requested = CI1302_PROTOCOL_MAX_PAYLOAD;
    int produced;
    uint16_t command;

    ci1302_ctx.stats.playback_requests++;
    if (payload_length >= sizeof(uint32_t)) {
        requested = ci1302_protocol_read_le32(payload);
    }
    if (requested == 0U || requested > sizeof(ci1302_pcm_buffer)) {
        requested = sizeof(ci1302_pcm_buffer);
    }
    if (!ci1302_ctx.playing || !ci1302_ctx.pcm_source) {
        ci1302_finish_playback();
        return;
    }

    produced = ci1302_ctx.pcm_source(ci1302_pcm_buffer, requested,
                                     ci1302_ctx.pcm_source_user_data);
    if (ci1302_ctx.streaming && produced == CI1302_PCM_AGAIN) {
        ci1302_ctx.pending_request = requested;
        return;
    }
    ci1302_ctx.pending_request = 0;
    if (produced <= 0) {
        ci1302_finish_playback();
        return;
    }
    if ((uint32_t)produced > requested) {
        produced = (int)requested;
    }
    command = (!ci1302_ctx.streaming && (uint32_t)produced < requested) ?
              CI1302_CMD_PLAY_DATA_END : CI1302_CMD_PLAY_DATA_RECV;
    if (ci1302_send_frame(command, ci1302_pcm_buffer,
                          (uint16_t)produced) == 0) {
        ci1302_ctx.stats.pcm_bytes += (uint32_t)produced;
    } else if (ci1302_ctx.streaming) {
        ci1302_ctx.playing = false;
        ci1302_ctx.pcm_source = NULL;
        ci1302_emit_event(CI1302_EVENT_ERROR, NULL, 0);
    }
    if (command == CI1302_CMD_PLAY_DATA_END) {
        ci1302_ctx.playing = false;
        ci1302_ctx.pcm_source = NULL;
        ci1302_ctx.pcm_source_user_data = NULL;
    }
}

static void ci1302_serve_playback_request(const uint8_t *payload, uint16_t length)
{
    if (os_mutex_lock(ci1302_ctx.play_mutex, CI1302_TX_TIMEOUT_MS) != 0) return;
    ci1302_serve_playback_locked(payload, length);
    os_mutex_unlock(ci1302_ctx.play_mutex);
}

static void ci1302_on_frame(const ci1302_frame_view_t *frame, void *user_data)
{
    (void)user_data;

    ci1302_ctx.stats.last_rx_command = frame->command;
    ci1302_ctx.stats.last_rx_payload_length = frame->payload_length;
    ci1302_ctx.stats.last_rx_version = frame->version;
    ci1302_ctx.stats.last_rx_fill_data = frame->fill_data;
    if (ci1302_ctx.trace_enabled) {
        os_printf(LM_CMD, LL_INFO,
                  "CI1302 RX cmd=0x%04x len=%u ver=0x%04x "
                  "fill=0x%08lx\r\n",
                  frame->command, frame->payload_length, frame->version,
                  (unsigned long)frame->fill_data);
    }

    switch (frame->command) {
    case CI1302_CMD_AUDIO_SYS_READY:
        ci1302_ctx.ready = true;
        ci1302_emit_event(CI1302_EVENT_READY, frame->payload,
                          frame->payload_length);
        break;
    case CI1302_CMD_AUDIO_SYS_ERROR:
        ci1302_emit_event(CI1302_EVENT_ERROR, frame->payload,
                          frame->payload_length);
        break;
    case CI1302_CMD_WAKE_UP:
        ci1302_emit_event(CI1302_EVENT_WAKE_UP, frame->payload,
                          frame->payload_length);
        break;
    case CI1302_CMD_EXIT_WAKE_UP:
        ci1302_emit_event(CI1302_EVENT_EXIT_WAKE_UP, frame->payload,
                          frame->payload_length);
        break;
    case CI1302_CMD_VAD_START:
        ci1302_ctx.recording = true;
        ci1302_emit_event(CI1302_EVENT_RECORD_START, NULL, 0);
        break;
    case CI1302_CMD_PCM_MIDDLE:
        ci1302_ctx.stats.record_frames++;
        ci1302_ctx.stats.record_bytes += frame->payload_length;
        if (frame->payload_length) {
            uint16_t index;
            for (index = 0; index < frame->payload_length; ++index) {
                ci1302_ctx.stats.record_hash ^= frame->payload[index];
                ci1302_ctx.stats.record_hash *= 16777619UL;
            }
        }
        ci1302_emit_event(CI1302_EVENT_RECORD_DATA, frame->payload,
                          frame->payload_length);
        break;
    case CI1302_CMD_PCM_FINISH:
        ci1302_ctx.recording = false;
        ci1302_emit_event(CI1302_EVENT_RECORD_STOP, NULL, 0);
        break;
    case CI1302_CMD_PLAY_DATA_GET:
        ci1302_serve_playback_request(frame->payload,
                                      frame->payload_length);
        break;
    case CI1302_CMD_NET_PLAY_END:
    case CI1302_CMD_PLAY_TTS_END:
        ci1302_ctx.playing = false;
        ci1302_ctx.pending_request = 0;
        ci1302_ctx.pcm_source = NULL;
        ci1302_emit_event(CI1302_EVENT_PLAY_STOP, NULL, 0);
        break;
    case CI1302_CMD_EXEC_STATE:
        if (frame->payload_length >= 3U && frame->payload[2] == 0x01U) {
            bool first_ready = !ci1302_ctx.ready;
            ci1302_ctx.ready = true;
            /* A chip already running may ACK before another READY broadcast.
             * Notify the adapter once so platform initialization can finish. */
            if (first_ready) ci1302_emit_event(CI1302_EVENT_READY, NULL, 0);
        }
        break;
    default:
        break;
    }
}

static void ci1302_rx_task(void *arg)
{
    (void)arg;

    while (ci1302_ctx.running) {
        int length = ci1302_uart_read(ci1302_ctx.rx_buffer,
                                     sizeof(ci1302_ctx.rx_buffer),
                                     ci1302_ctx.pending_request ? 5 : 100);
        if (length > 0) {
            ci1302_ctx.stats.uart_rx_bytes += (uint32_t)length;
            if (ci1302_ctx.raw_trace_enabled) {
                ci1302_trace_raw_rx(ci1302_ctx.rx_buffer, (size_t)length);
            }
            ci1302_protocol_feed(&ci1302_ctx.parser,
                                 ci1302_ctx.rx_buffer, (size_t)length);
        }
        if (ci1302_ctx.pending_request && ci1302_ctx.playing) {
            uint32_t requested = ci1302_ctx.pending_request;
            uint8_t request[4] = { requested, requested >> 8,
                                   requested >> 16, requested >> 24 };
            /* A retry serves the same chip request, not a new request. */
            ci1302_ctx.stats.playback_requests--;
            ci1302_serve_playback_request(request, sizeof(request));
        }
    }
    ci1302_ctx.rx_task = 0;
    os_task_delete(os_task_get_running_handle());
}

int ci1302_init(void)
{
    if (ci1302_ctx.initialized) {
        return 0;
    }
    memset(&ci1302_ctx, 0, sizeof(ci1302_ctx));
    ci1302_protocol_parser_init(&ci1302_ctx.parser, ci1302_on_frame, NULL);
    ci1302_ctx.stats.record_hash = 2166136261UL;
    ci1302_ctx.volume = 5;
    ci1302_ctx.mutex = os_mutex_create();
    ci1302_ctx.play_mutex = os_mutex_create();
    if (!ci1302_ctx.mutex || !ci1302_ctx.play_mutex) {
        if (ci1302_ctx.mutex) os_mutex_destroy(ci1302_ctx.mutex);
        if (ci1302_ctx.play_mutex) os_mutex_destroy(ci1302_ctx.play_mutex);
        memset(&ci1302_ctx, 0, sizeof(ci1302_ctx));
        return -1;
    }

    if (ci1302_uart_open() != 0) {
        os_mutex_destroy(ci1302_ctx.play_mutex);
        os_mutex_destroy(ci1302_ctx.mutex);
        memset(&ci1302_ctx, 0, sizeof(ci1302_ctx));
        return -1;
    }

    ci1302_ctx.initialized = true;
    ci1302_ctx.running = true;
    ci1302_ctx.rx_task = os_task_create("ci1302-rx", 7,
                                        CI1302_RX_TASK_STACK_SIZE,
                                        ci1302_rx_task, NULL);
    if (ci1302_ctx.rx_task == -1) {
        ci1302_ctx.running = false;
        ci1302_ctx.initialized = false;
        ci1302_uart_close();
        os_mutex_destroy(ci1302_ctx.play_mutex);
        os_mutex_destroy(ci1302_ctx.mutex);
        memset(&ci1302_ctx, 0, sizeof(ci1302_ctx));
        return -1;
    }

    os_printf(LM_APP, LL_INFO,
              "CI1302 UART1 initialized at %d baud\r\n",
              CONFIG_CI1302_UART_BAUDRATE);
    return 0;
}

int ci1302_deinit(void)
{
    int wait_count = 50;

    if (!ci1302_ctx.initialized) {
        return 0;
    }
    if (platform_callback) platform_callback(CI1302_EVENT_ERROR, NULL, 0, platform_user_data);
    ci1302_ctx.running = false;
    while (ci1302_ctx.rx_task && wait_count--) {
        os_msleep(10);
    }
    if (ci1302_ctx.rx_task) {
        os_task_delete(ci1302_ctx.rx_task);
        ci1302_ctx.rx_task = 0;
    }
    ci1302_uart_close();
    os_mutex_destroy(ci1302_ctx.play_mutex);
    os_mutex_destroy(ci1302_ctx.mutex);
    memset(&ci1302_ctx, 0, sizeof(ci1302_ctx));
    return 0;
}

bool ci1302_is_initialized(void)
{
    return ci1302_ctx.initialized;
}

bool ci1302_is_ready(void)
{
    return ci1302_ctx.ready;
}

bool ci1302_is_recording(void)
{
    return ci1302_ctx.recording;
}

bool ci1302_is_playing(void)
{
    return ci1302_ctx.playing;
}

uint8_t ci1302_get_volume(void)
{
    return ci1302_ctx.volume;
}

void ci1302_set_event_callback(ci1302_event_callback_t callback,
                               void *user_data)
{
    ci1302_ctx.callback = callback;
    ci1302_ctx.callback_user_data = user_data;
}

void ci1302_set_platform_callback(ci1302_event_callback_t callback, void *user_data)
{
    platform_user_data = user_data;
    platform_callback = callback;
}

void ci1302_set_trace(bool enabled)
{
    ci1302_ctx.trace_enabled = enabled;
}

void ci1302_set_raw_trace(bool enabled)
{
    ci1302_ctx.raw_trace_enabled = enabled;
}

int ci1302_record_start(void)
{
    int ret = ci1302_send_frame(CI1302_CMD_SET_AUDIO_START_RECORD, NULL, 0);

    ci1302_ctx.recording = (ret == 0);
    return ret;
}

int ci1302_record_stop(void)
{
    return ci1302_send_frame(CI1302_CMD_SET_AUDIO_STOP_RECORD, NULL, 0);
}

int ci1302_set_volume(uint8_t volume)
{
    if (volume < 1U || volume > 7U) {
        return -1;
    }
    ci1302_ctx.volume = volume;
    return ci1302_send_frame(CI1302_CMD_SET_AUDIO_VOLUME, &volume, 1);
}

static int ci1302_play_start(ci1302_pcm_source_t source, void *user_data,
                             bool streaming)
{
    if (!source || !ci1302_ctx.initialized) return -1;
    if (os_mutex_lock(ci1302_ctx.play_mutex, CI1302_TX_TIMEOUT_MS) != 0) return -1;
    if (ci1302_ctx.playing) {
        os_mutex_unlock(ci1302_ctx.play_mutex);
        return -1;
    }
    ci1302_ctx.pcm_source = source;
    ci1302_ctx.pcm_source_user_data = user_data;
    ci1302_ctx.streaming = streaming;
    ci1302_ctx.pending_request = 0;
    ci1302_ctx.playing = true;
    ci1302_emit_event(CI1302_EVENT_PLAY_START, NULL, 0);
    if (ci1302_send_frame(CI1302_CMD_NET_PLAY_START, NULL, 0) != 0) {
        ci1302_ctx.playing = false;
        ci1302_ctx.pcm_source = NULL;
        ci1302_ctx.pcm_source_user_data = NULL;
        os_mutex_unlock(ci1302_ctx.play_mutex);
        return -1;
    }
    os_mutex_unlock(ci1302_ctx.play_mutex);
    return 0;
}

int ci1302_play_pcm_start(ci1302_pcm_source_t source, void *user_data)
{
    return ci1302_play_start(source, user_data, false);
}

int ci1302_play_pcm_stream_start(ci1302_pcm_source_t source, void *user_data)
{
    return ci1302_play_start(source, user_data, true);
}

int ci1302_play_stop(void)
{
    int ret;
    if (!ci1302_ctx.initialized) return -1;
    if (os_mutex_lock(ci1302_ctx.play_mutex, CI1302_TX_TIMEOUT_MS) != 0) return -1;
    ci1302_ctx.pending_request = 0;
    ci1302_ctx.playing = false;
    ci1302_ctx.pcm_source = NULL;
    ci1302_ctx.pcm_source_user_data = NULL;
    ret = ci1302_send_frame(CI1302_CMD_NET_PLAY_STOP, NULL, 0);
    os_mutex_unlock(ci1302_ctx.play_mutex);
    return ret;
}

int ci1302_play_tone(uint32_t duration_ms, uint8_t volume)
{
    if (duration_ms == 0U || duration_ms > CI1302_TONE_MAX_DURATION_MS ||
        ci1302_set_volume(volume) != 0) {
        return -1;
    }
    ci1302_ctx.tone_samples_remaining =
        (duration_ms * CI1302_TONE_SAMPLE_RATE) / 1000U;
    ci1302_ctx.tone_sample_index = 0;
    return ci1302_play_pcm_start(ci1302_tone_source, &ci1302_ctx);
}

int ci1302_reset(void)
{
    ci1302_ctx.ready = false;
    return ci1302_send_frame(CI1302_CMD_AUDIO_RESET, NULL, 0);
}

void ci1302_get_stats(ci1302_stats_t *stats)
{
    if (!stats) {
        return;
    }
    *stats = ci1302_ctx.stats;
    stats->protocol_frames = ci1302_ctx.parser.stats.frames_received;
    stats->protocol_discarded_bytes = ci1302_ctx.parser.stats.bytes_discarded;
    stats->protocol_length_errors = ci1302_ctx.parser.stats.length_errors;
    stats->protocol_checksum_errors = ci1302_ctx.parser.stats.checksum_errors;
    stats->trace_enabled = ci1302_ctx.trace_enabled;
    stats->raw_trace_enabled = ci1302_ctx.raw_trace_enabled;
}

void ci1302_reset_stats(void)
{
    memset(&ci1302_ctx.stats, 0, sizeof(ci1302_ctx.stats));
    memset(&ci1302_ctx.parser.stats, 0, sizeof(ci1302_ctx.parser.stats));
    ci1302_ctx.stats.record_hash = 2166136261UL;
}
