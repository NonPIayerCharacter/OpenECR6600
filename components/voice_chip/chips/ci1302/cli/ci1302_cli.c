#include "ci1302.h"
#include "ci1302_audio.h"

#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "ci1302_sample_pcm.h"
#include "oshal.h"

#define CI1302_PCM_SAMPLE_RATE         16000U
#define CI1302_PCM_BYTES_PER_SAMPLE    2U
#define CI1302_RECORD_SECONDS          1U
#define CI1302_CAPTURE_SIZE            \
    (CI1302_PCM_SAMPLE_RATE * CI1302_PCM_BYTES_PER_SAMPLE * \
     CI1302_RECORD_SECONDS)
#define CI1302_RECORD_STOP_TIMEOUT_MS  1000U
#define CI1302_CLI_RAW_MAX_PAYLOAD     16U

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} ci1302_pcm_stream_t;

static uint8_t ci1302_capture[CI1302_CAPTURE_SIZE];
static size_t ci1302_capture_used;
static uint32_t ci1302_capture_frames;
static uint32_t ci1302_capture_dropped;
static ci1302_pcm_stream_t ci1302_playback;

static void ci1302_cli_event(ci1302_event_t event, const uint8_t *data,
                             uint16_t length, void *user_data);

static int ci1302_cli_parse_value(const char *text, unsigned long maximum,
                                  unsigned long *value)
{
    char *end;
    unsigned long parsed;

    if (!text || !text[0] || text[0] == '-') {
        return -1;
    }
    parsed = strtoul(text, &end, 0);
    if (*end != '\0' || parsed > maximum) {
        return -1;
    }
    *value = parsed;
    return 0;
}

static void ci1302_cli_reset_capture(void)
{
    ci1302_reset_stats();
    ci1302_capture_used = 0;
    ci1302_capture_frames = 0;
    ci1302_capture_dropped = 0;
    ci1302_set_event_callback(ci1302_cli_event, NULL);
}

static void ci1302_cli_event(ci1302_event_t event, const uint8_t *data,
                             uint16_t length, void *user_data)
{
    (void)user_data;

    if (event == CI1302_EVENT_RECORD_DATA) {
        size_t copy_length = length;

        if (ci1302_capture_used >= sizeof(ci1302_capture)) {
            ci1302_capture_dropped++;
            return;
        }
        if (copy_length > sizeof(ci1302_capture) - ci1302_capture_used) {
            copy_length = sizeof(ci1302_capture) - ci1302_capture_used;
            ci1302_capture_dropped++;
        }
        memcpy(ci1302_capture + ci1302_capture_used, data, copy_length);
        ci1302_capture_used += copy_length;
        ci1302_capture_frames++;
    }
}

static int ci1302_pcm_stream_source(uint8_t *buffer, uint32_t max_length,
                                    void *user_data)
{
    ci1302_pcm_stream_t *stream = (ci1302_pcm_stream_t *)user_data;
    size_t copy_length;

    if (stream->offset >= stream->length) {
        return 0;
    }
    copy_length = stream->length - stream->offset;
    if (copy_length > max_length) {
        copy_length = max_length;
    }
    memcpy(buffer, stream->data + stream->offset, copy_length);
    stream->offset += copy_length;
    return (int)copy_length;
}

static int ci1302_cli_start_pcm(const uint8_t *data, size_t length,
                                uint8_t volume)
{
    if (!data || length == 0U || !ci1302_is_initialized() ||
        ci1302_is_recording() || ci1302_is_playing()) {
        return -1;
    }
    if (ci1302_set_volume(volume) != 0) {
        return -1;
    }
    ci1302_playback.data = data;
    ci1302_playback.length = length;
    ci1302_playback.offset = 0;
    return ci1302_play_pcm_start(ci1302_pcm_stream_source,
                                 &ci1302_playback);
}

static void ci1302_cli_print_stats(void)
{
    ci1302_stats_t stats;

    ci1302_get_stats(&stats);
    os_printf(LM_CMD, LL_INFO,
              "CI1302 initialized=%d ready=%d recording=%d playing=%d "
              "volume=%u trace=%d rawtrace=%d\r\n",
              ci1302_is_initialized(), ci1302_is_ready(),
              ci1302_is_recording(), ci1302_is_playing(),
              ci1302_get_volume(), stats.trace_enabled,
              stats.raw_trace_enabled);
    os_printf(LM_CMD, LL_INFO,
              "uart rx=%lu tx=%lu frames=%lu discard=%lu "
              "len_err=%lu checksum_err=%lu\r\n",
              (unsigned long)stats.uart_rx_bytes,
              (unsigned long)stats.uart_tx_bytes,
              (unsigned long)stats.protocol_frames,
              (unsigned long)stats.protocol_discarded_bytes,
              (unsigned long)stats.protocol_length_errors,
              (unsigned long)stats.protocol_checksum_errors);
    os_printf(LM_CMD, LL_INFO,
              "last_rx cmd=0x%04x len=%u ver=0x%04x fill=0x%08lx\r\n",
              stats.last_rx_command, stats.last_rx_payload_length,
              stats.last_rx_version, (unsigned long)stats.last_rx_fill_data);
    os_printf(LM_CMD, LL_INFO,
              "record pcm_frames=%lu pcm_bytes=%lu hash=%08lx "
              "play pcm_bytes=%lu requests=%lu\r\n",
              (unsigned long)stats.record_frames,
              (unsigned long)stats.record_bytes,
              (unsigned long)stats.record_hash,
              (unsigned long)stats.pcm_bytes,
              (unsigned long)stats.playback_requests);
    os_printf(LM_CMD, LL_INFO,
              "capture frames=%lu bytes=%lu dropped=%lu\r\n",
              (unsigned long)ci1302_capture_frames,
              (unsigned long)ci1302_capture_used,
              (unsigned long)ci1302_capture_dropped);
}

static int ci1302_cli_init(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return ci1302_init() == 0 ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int ci1302_cli_deinit(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = ci1302_deinit();
    return ret == 0 ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int ci1302_cli_status(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    ci1302_cli_print_stats();
    return CMD_RET_SUCCESS;
}

static int ci1302_cli_record(int argc, char *argv[])
{
    unsigned long duration_seconds = CI1302_RECORD_SECONDS;
    unsigned int waited_ms = 0;

    if (argc > 2 || (argc == 2 &&
        (ci1302_cli_parse_value(argv[1], CI1302_RECORD_SECONDS,
                                &duration_seconds) != 0 ||
         duration_seconds != CI1302_RECORD_SECONDS))) {
        return CMD_RET_USAGE;
    }
    if (!ci1302_is_initialized() || ci1302_is_playing()) {
        os_printf(LM_CMD, LL_ERR,
                  "CI1302 must be initialized and idle before recording\r\n");
        return CMD_RET_FAILURE;
    }
    ci1302_cli_reset_capture();
    if (ci1302_record_start() != 0) {
        return CMD_RET_FAILURE;
    }
    os_printf(LM_CMD, LL_INFO, "CI1302 recording for %lu seconds\r\n",
              duration_seconds);
    os_msleep((unsigned int)duration_seconds * 1000U);
    if (ci1302_record_stop() != 0) {
        return CMD_RET_FAILURE;
    }
    while (ci1302_is_recording() &&
           waited_ms < CI1302_RECORD_STOP_TIMEOUT_MS) {
        os_msleep(10);
        waited_ms += 10U;
    }
    ci1302_cli_print_stats();
    if (ci1302_capture_used == 0U) {
        os_printf(LM_CMD, LL_ERR, "CI1302 captured no PCM data\r\n");
        return CMD_RET_FAILURE;
    }
    os_printf(LM_CMD, LL_INFO,
              "CI1302 1s PCM capture ready: %lu/%u bytes\r\n",
              (unsigned long)ci1302_capture_used, CI1302_CAPTURE_SIZE);
    return CMD_RET_SUCCESS;
}

static int ci1302_cli_recstart(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    ci1302_cli_reset_capture();
    return ci1302_record_start() == 0 ?
           CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int ci1302_cli_recstop(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    if (ci1302_record_stop() != 0) {
        return CMD_RET_FAILURE;
    }
    os_msleep(200);
    ci1302_cli_print_stats();
    return CMD_RET_SUCCESS;
}

static int ci1302_cli_replay(int argc, char *argv[])
{
    unsigned long volume = ci1302_get_volume();

    if (argc > 2 || (argc == 2 &&
        (ci1302_cli_parse_value(argv[1], 7U, &volume) != 0 ||
         volume == 0U))) {
        return CMD_RET_USAGE;
    }
    if (ci1302_capture_used == 0U) {
        os_printf(LM_CMD, LL_ERR, "CI1302 capture buffer is empty\r\n");
        return CMD_RET_FAILURE;
    }
    ci1302_set_event_callback(ci1302_cli_event, NULL);
    if (ci1302_cli_start_pcm(ci1302_capture, ci1302_capture_used,
                             (uint8_t)volume) != 0) {
        return CMD_RET_FAILURE;
    }
    os_printf(LM_CMD, LL_INFO,
              "CI1302 PCM replay started: %lu bytes, volume %lu\r\n",
              (unsigned long)ci1302_capture_used, volume);
    return CMD_RET_SUCCESS;
}

static int ci1302_cli_sample(int argc, char *argv[])
{
    unsigned long volume = ci1302_get_volume();
    size_t sample_size = ci1302_sample_pcm_size();

    if (argc > 2 || (argc == 2 &&
        (ci1302_cli_parse_value(argv[1], 7U, &volume) != 0 ||
         volume == 0U))) {
        return CMD_RET_USAGE;
    }
    if (ci1302_cli_start_pcm(ci1302_sample_pcm_data(), sample_size,
                             (uint8_t)volume) != 0) {
        return CMD_RET_FAILURE;
    }
    os_printf(LM_CMD, LL_INFO,
              "CI1302 PCM sample started: %lu bytes, volume %lu\r\n",
              (unsigned long)sample_size, volume);
    return CMD_RET_SUCCESS;
}

static int ci1302_cli_tone(int argc, char *argv[])
{
    unsigned long duration_ms;
    unsigned long volume = 5;

    if (argc < 2 || argc > 3 ||
        ci1302_cli_parse_value(argv[1], 60000U, &duration_ms) != 0 ||
        duration_ms == 0U) {
        return CMD_RET_USAGE;
    }
    if (argc == 3 &&
        ci1302_cli_parse_value(argv[2], 7U, &volume) != 0) {
        return CMD_RET_USAGE;
    }
    if (volume == 0U ||
        ci1302_play_tone((uint32_t)duration_ms, (uint8_t)volume) != 0) {
        return CMD_RET_FAILURE;
    }
    os_printf(LM_CMD, LL_INFO,
              "CI1302 1kHz tone started: %lu ms, volume %lu\r\n",
              duration_ms, volume);
    return CMD_RET_SUCCESS;
}

static int ci1302_cli_stop(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return ci1302_play_stop() == 0 ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int ci1302_cli_volume(int argc, char *argv[])
{
    unsigned long volume;

    if (argc != 2 ||
        ci1302_cli_parse_value(argv[1], 7U, &volume) != 0 ||
        volume == 0U) {
        return CMD_RET_USAGE;
    }
    return ci1302_set_volume((uint8_t)volume) == 0 ?
           CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int ci1302_cli_trace(int argc, char *argv[])
{
    unsigned long enabled;

    if (argc != 2 ||
        ci1302_cli_parse_value(argv[1], 1U, &enabled) != 0) {
        return CMD_RET_USAGE;
    }
    ci1302_set_trace(enabled != 0U);
    os_printf(LM_CMD, LL_INFO, "CI1302 trace %s\r\n",
              enabled ? "enabled" : "disabled");
    return CMD_RET_SUCCESS;
}

static int ci1302_cli_raw_trace(int argc, char *argv[])
{
    unsigned long enabled;

    if (argc != 2 ||
        ci1302_cli_parse_value(argv[1], 1U, &enabled) != 0) {
        return CMD_RET_USAGE;
    }
    ci1302_set_raw_trace(enabled != 0U);
    os_printf(LM_CMD, LL_INFO, "CI1302 raw trace %s\r\n",
              enabled ? "enabled" : "disabled");
    return CMD_RET_SUCCESS;
}

static int ci1302_cli_send(int argc, char *argv[])
{
    uint8_t payload[CI1302_CLI_RAW_MAX_PAYLOAD];
    unsigned long command;
    unsigned long fill_data;
    int index;

    if (argc < 3 ||
        (unsigned int)(argc - 3) > sizeof(payload) ||
        ci1302_cli_parse_value(argv[1], UINT16_MAX, &command) != 0 ||
        ci1302_cli_parse_value(argv[2], UINT32_MAX, &fill_data) != 0) {
        return CMD_RET_USAGE;
    }
    for (index = 3; index < argc; ++index) {
        unsigned long byte;

        if (ci1302_cli_parse_value(argv[index], UINT8_MAX, &byte) != 0) {
            return CMD_RET_USAGE;
        }
        payload[index - 3] = (uint8_t)byte;
    }
    return ci1302_send_command((uint16_t)command, (uint32_t)fill_data,
                               payload, (uint16_t)(argc - 3)) == 0 ?
           CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int ci1302_cli_reset(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return ci1302_reset() == 0 ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

/* Exercise the platform hardware endpoint without starting any cloud tasks.
 * Reuse the existing 32000-byte CLI capture; do not add a test audio buffer. */
static int ci1302_cli_port(int argc, char *argv[])
{
    unsigned long volume = 5;
    const uint8_t *data;
    size_t length;
    if (argc < 2 || argc > 3) return CMD_RET_USAGE;
    if (!strcmp(argv[1], "status")) {
        os_printf(LM_CMD, LL_INFO, "CI1302 adapter status=%d rx_overflow=%lu\r\n",
                  ci1302_audio_status(), (unsigned long)ci1302_audio_rx_overflow());
        return CMD_RET_SUCCESS;
    }
    if (ci1302_audio_init() != 0) return CMD_RET_FAILURE;
    if (!strcmp(argv[1], "stop")) {
        ci1302_audio_capture_stop();
        ci1302_audio_cancel();
        return CMD_RET_SUCCESS;
    }
    if (ci1302_is_recording() || ci1302_is_playing()) return CMD_RET_FAILURE;
    if (!strcmp(argv[1], "record")) {
        if (argc != 2) return CMD_RET_USAGE;
        ci1302_capture_used = ci1302_capture_frames = ci1302_capture_dropped = 0;
        ci1302_reset_stats();
        ci1302_set_event_callback(NULL, NULL);
        if (ci1302_audio_capture_begin() != 0) return CMD_RET_FAILURE;
        while (ci1302_capture_used < sizeof(ci1302_capture)) {
            size_t count = sizeof(ci1302_capture) - ci1302_capture_used;
            if (count > 512) count = 512;
            if (ci1302_audio_read(ci1302_capture + ci1302_capture_used, count) != 0) break;
            ci1302_capture_used += count;
        }
        ci1302_audio_capture_stop();
        ci1302_stats_t stats;
        ci1302_get_stats(&stats);
        ci1302_capture_frames = stats.record_frames;
        os_printf(LM_CMD, LL_INFO, "CI1302 adapter capture: %lu/32000 bytes, overflow=%lu\r\n",
                  (unsigned long)ci1302_capture_used, (unsigned long)ci1302_audio_rx_overflow());
        return ci1302_capture_used == sizeof(ci1302_capture) && !ci1302_audio_rx_overflow() ?
               CMD_RET_SUCCESS : CMD_RET_FAILURE;
    }
    if (argc == 3 && (ci1302_cli_parse_value(argv[2], 7, &volume) || !volume)) return CMD_RET_USAGE;
    if (!strcmp(argv[1], "sample")) {
        data = ci1302_sample_pcm_data();
        length = ci1302_sample_pcm_size();
    } else if (!strcmp(argv[1], "replay")) {
        data = ci1302_capture;
        length = ci1302_capture_used;
    } else return CMD_RET_USAGE;
    if (!length || ci1302_set_volume(volume) != 0) return CMD_RET_FAILURE;
    while (length) {
        size_t count = length > 2048 ? 2048 : length;
        if (ci1302_audio_write(data, count) != 0) return CMD_RET_FAILURE;
        data += count;
        length -= count;
    }
    ci1302_audio_drain();
    return CMD_RET_SUCCESS;
}

CLI_SUBCMD(ci1302, port, ci1302_cli_port,
           "test platform hardware adapter", "ci1302 port <sample [1-7]|record|replay [1-7]|stop|status>");
CLI_SUBCMD(ci1302, init, ci1302_cli_init,
           "initialize CI1302", "ci1302 init");
CLI_SUBCMD(ci1302, deinit, ci1302_cli_deinit,
           "deinitialize CI1302", "ci1302 deinit");
CLI_SUBCMD(ci1302, status, ci1302_cli_status,
           "show CI1302 statistics", "ci1302 status");
CLI_SUBCMD(ci1302, sample, ci1302_cli_sample,
           "play built-in PCM sample", "ci1302 sample [volume:1-7]");
CLI_SUBCMD(ci1302, record, ci1302_cli_record,
           "capture one second of PCM", "ci1302 record [1]");
CLI_SUBCMD(ci1302, recstart, ci1302_cli_recstart,
           "start PCM capture", "ci1302 recstart");
CLI_SUBCMD(ci1302, recstop, ci1302_cli_recstop,
           "stop PCM capture", "ci1302 recstop");
CLI_SUBCMD(ci1302, replay, ci1302_cli_replay,
           "replay captured PCM", "ci1302 replay [volume:1-7]");
CLI_SUBCMD(ci1302, tone, ci1302_cli_tone,
           "play local PCM test tone", "ci1302 tone <ms:1-60000> [volume:1-7]");
CLI_SUBCMD(ci1302, stop, ci1302_cli_stop,
           "stop CI1302 playback", "ci1302 stop");
CLI_SUBCMD(ci1302, volume, ci1302_cli_volume,
           "set CI1302 volume", "ci1302 volume <1-7>");
CLI_SUBCMD(ci1302, reset, ci1302_cli_reset,
           "software reset CI1302", "ci1302 reset");
CLI_SUBCMD(ci1302, trace, ci1302_cli_trace,
           "trace CI1302 protocol frames", "ci1302 trace <0|1>");
CLI_SUBCMD(ci1302, rawtrace, ci1302_cli_raw_trace,
           "trace every CI1302 UART RX byte",
           "ci1302 rawtrace <0|1>");
CLI_SUBCMD(ci1302, send, ci1302_cli_send,
           "send a raw CI1302 protocol frame",
           "ci1302 send <cmd> <fill> [byte0 ... byte15]");
CLI_CMD(ci1302, NULL, "CI1302 audio test", "ci1302 <subcommand>");
