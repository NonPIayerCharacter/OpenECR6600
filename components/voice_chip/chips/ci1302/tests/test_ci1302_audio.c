/* Deterministic UART peer: run real protocol, driver and adapter without a board.
 * Only the RTOS/physical UART are substituted. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ci1302.c"
#include "ci1302_audio.h"
#include "voice_ci1302_service.h"

static unsigned int sent, ends, starts, cli_events;
static uint8_t received[32768];
static size_t received_length;
static bool auto_pull, cancel_next, stop_capture_next;
static bool fail_pcm;
static unsigned int ready_events;
static unsigned int service_record_bytes, service_finished;

static void service_record(uint8_t *data, uint16_t length)
{
    assert(data && length);
    service_record_bytes += length;
    os_free(data);
}

static void service_complete(void) { ++service_finished; }
static uint8_t capture_byte;

static void feed(uint16_t command, const void *data, uint16_t length)
{
    uint8_t frame[CI1302_PROTOCOL_MAX_FRAME_SIZE];
    int size = ci1302_protocol_encode(command, data, length,
               CI1302_PROTOCOL_FILL_DEFAULT, frame, sizeof(frame));
    assert(size > 0);
    ci1302_protocol_feed(&ci1302_ctx.parser, frame, (size_t)size);
}

static void pull(void)
{
    uint8_t request[] = {0, 16, 0, 0}; /* 4096 bytes */
    feed(CI1302_CMD_PLAY_DATA_GET, request, sizeof(request));
}

int ci1302_uart_open(void) { return 0; }
int ci1302_uart_close(void) { return 0; }
int ci1302_uart_read(void *data, unsigned int length, unsigned int timeout)
{ (void)data; (void)length; (void)timeout; return 0; }
int ci1302_uart_write(const void *data, unsigned int length)
{
    const uint8_t *frame = data;
    uint16_t command = ci1302_protocol_read_le16(frame + 6);
    uint16_t size = ci1302_protocol_read_le16(frame + 8);
    assert(length == size + 16U);
    ++sent;
    if (command == CI1302_CMD_NET_PLAY_START) ++starts;
    if (command == CI1302_CMD_PLAY_DATA_END) ++ends;
    if (command == CI1302_CMD_PLAY_DATA_RECV || command == CI1302_CMD_PLAY_DATA_END) {
        if (fail_pcm) { fail_pcm = false; return -1; }
        assert(received_length + size <= sizeof(received));
        memcpy(received + received_length, frame + 16, size);
        received_length += size;
    }
    return 0;
}

os_mutex_handle_t os_mutex_create(void) { return calloc(1, sizeof(int)); }
int os_mutex_destroy(os_mutex_handle_t mutex) { free(mutex); return 0; }
int os_mutex_lock(os_mutex_handle_t mutex, unsigned int timeout)
{
    (void)timeout;
    assert(mutex && !*(int *)mutex);
    *(int *)mutex = 1;
    return 0;
}
int os_mutex_unlock(os_mutex_handle_t mutex)
{
    assert(mutex && *(int *)mutex);
    *(int *)mutex = 0;
    return 0;
}
int os_task_create(const char *name, int priority, int stack, task_entry_t entry, void *arg)
{ (void)name; (void)priority; (void)stack; (void)entry; (void)arg; return 1; }
int os_task_delete(int task) { (void)task; return 0; }
int os_task_get_running_handle(void) { return 1; }
int os_printf(int module, int level, const char *fmt, ...)
{ (void)module; (void)level; (void)fmt; return 0; }
void os_msleep(unsigned int ms)
{
    (void)ms;
    if (cancel_next) { cancel_next = false; ci1302_audio_cancel(); return; }
    if (stop_capture_next) {
        stop_capture_next = false;
        ci1302_audio_capture_stop();
        return;
    }
    if (auto_pull && ci1302_is_playing()) pull();
    if (ci1302_is_recording()) {
        uint8_t pcm[128];
        for (unsigned int i = 0; i < sizeof(pcm); ++i) pcm[i] = capture_byte++;
        feed(CI1302_CMD_PCM_MIDDLE, pcm, sizeof(pcm));
    }
}
static void cli_event(ci1302_event_t event, const uint8_t *data, uint16_t length, void *arg)
{
    (void)data; (void)length; (void)arg;
    if (event == CI1302_EVENT_RECORD_DATA) ++cli_events;
}

static int finite_source(uint8_t *data, uint32_t maximum, void *arg)
{
    (void)arg;
    assert(maximum >= 2);
    data[0] = 0x12;
    data[1] = 0x34;
    return 2;
}

static void adapter_event(ci1302_event_t event, const uint8_t *data, uint16_t length, void *arg)
{
    (void)data; (void)length; (void)arg;
    if (event == CI1302_EVENT_READY) ++ready_events;
}

int main(void)
{
    uint8_t pcm[14210], recorded[512];
    for (size_t i = 0; i < sizeof(pcm); ++i) pcm[i] = (uint8_t)i;
    assert(ci1302_audio_init() == 0);
    ci1302_audio_on_event(adapter_event, NULL);
    const uint8_t ack[] = {0x17, 0x01, 0x01};
    feed(CI1302_CMD_EXEC_STATE, ack, sizeof(ack));
    feed(CI1302_CMD_EXEC_STATE, ack, sizeof(ack));
    assert(ci1302_is_ready() && ready_events == 1);
    auto_pull = true;
    /* Caller-owned blocks may be released after each synchronous write. Short
     * blocks must not terminate a streaming session. */
    assert(ci1302_audio_write(pcm, 2048) == 0);
    unsigned int before = sent;
    pull();
    assert(sent == before); /* starvation is not EOF */
    assert(ci1302_ctx.pending_request == 4096);
    assert(ci1302_audio_write(pcm + 2048, sizeof(pcm) - 2048) == 0);
    assert(starts == 1 && ends == 0);
    assert(received_length == sizeof(pcm));
    assert(memcmp(received, pcm, sizeof(pcm)) == 0);
    ci1302_audio_drain();
    pull();
    assert(ends == 1 && ci1302_is_playing());
    feed(CI1302_CMD_PLAY_TTS_END, NULL, 0);
    assert(!ci1302_is_playing());

    /* Cancel releases the borrowed producer buffer and permits a new session. */
    auto_pull = false;
    cancel_next = true;
    assert(ci1302_audio_write(pcm, 2048) == -1);
    before = (unsigned int)received_length;
    pull();
    assert(received_length == before);
    auto_pull = true;
    assert(ci1302_audio_write(pcm, 2048) == 0);
    ci1302_audio_cancel();

    fail_pcm = true;
    assert(ci1302_audio_write(pcm, 2048) == -1);
    assert(!ci1302_is_playing());
    assert(ci1302_audio_write(pcm, 2048) == 0);
    ci1302_audio_cancel();

    /* Existing CLI finite-source short-read contract is unchanged. */
    before = ends;
    assert(ci1302_play_pcm_start(finite_source, NULL) == 0);
    pull();
    assert(ends == before + 1 && !ci1302_is_playing());

    /* CLI's subscriber does not replace the hardware capture adapter. */
    ci1302_set_event_callback(cli_event, NULL);
    assert(ci1302_audio_capture_begin() == 0);
    assert(ci1302_audio_read(recorded, sizeof(recorded)) == 0);
    assert(cli_events == 4);
    for (size_t i = 0; i < sizeof(recorded); ++i) assert(recorded[i] == (uint8_t)i);
    stop_capture_next = true;
    memset(recorded, 0xff, sizeof(recorded));
    assert(ci1302_audio_read(recorded, sizeof(recorded)) == -1);
    for (size_t i = 0; i < sizeof(recorded); ++i) assert(recorded[i] == 0);
    feed(CI1302_CMD_PCM_FINISH, NULL, 0);
    assert(ci1302_audio_capture_begin() == 0);
    feed(CI1302_CMD_PCM_MIDDLE, pcm, 4096);
    feed(CI1302_CMD_PCM_MIDDLE, pcm, 128);
    assert(ci1302_audio_rx_overflow() == 128);
    assert(ci1302_audio_read(recorded, sizeof(recorded)) == -1);
    ci1302_audio_capture_stop();
    feed(CI1302_CMD_PCM_FINISH, NULL, 0);
    ci1302_ctx.rx_task = 0;
    assert(ci1302_deinit() == 0);
    assert(ci1302_audio_init() == 0);
    assert(ci1302_audio_write(pcm, 2048) == 0);
    ci1302_audio_cancel();
    /* Exercise the public service, including automatic backend registration. */
    assert(voice_chip_service_start() == 0);
    feed(CI1302_CMD_AUDIO_SYS_READY, NULL, 0);
    assert(voice_chip_service_is_ready());
    assert(voice_chip_service_get_caps()->capture.sample_rate_hz == 16000);
    assert(voice_chip_service_get_caps()->transport_flags == VOICE_CHIP_TRANSPORT_UART);
    assert(voice_chip_service_set_output_volume(0) == -1);
    assert(voice_chip_service_set_output_volume(1) == 0);
    assert(voice_chip_service_get_output_volume() == 1);
    assert(voice_chip_service_set_output_volume(100) == 0);
    assert(ci1302_get_volume() == 7);
    assert(voice_chip_service_play_local(9) == -1);
    assert(voice_chip_service_play_stream(pcm, 128, AUDIO_FORMAT_OPUS) == -1);
    assert(voice_chip_service_start_capture() == -1); /* No data consumer. */
    assert(voice_chip_service_set_record_callbacks(NULL, service_record, NULL) == 0);
    assert(voice_chip_service_start_capture() == 0);
    feed(CI1302_CMD_PCM_MIDDLE, pcm, 128);
    assert(service_record_bytes == 128);
    assert(voice_chip_service_play_stream(pcm, 128, AUDIO_FORMAT_PCM) == -1);
    assert(voice_chip_service_stop_capture() == 0);
    feed(CI1302_CMD_PCM_FINISH, NULL, 0);
    assert(voice_chip_service_set_playback_callbacks(NULL, service_complete) == 0);
    received_length = 0;
    assert(voice_chip_service_play_stream(pcm, 2048, AUDIO_FORMAT_PCM) == 0);
    assert(voice_chip_service_end_stream() == 0);
    pull();
    assert(service_finished == 0 && voice_chip_service_get_status() == 2);
    feed(CI1302_CMD_PLAY_TTS_END, NULL, 0);
    assert(service_finished == 1 && voice_chip_service_get_status() == 0);
    assert(voice_ci1302_service_last_error() == 0);
    ci1302_ctx.rx_task = 0;
    assert(voice_chip_service_deinit() == 0);
    assert(voice_chip_service_start() == 0);
    puts("CI1302 protocol/PCM/service: integrity, EOF, cancel, capture ownership, capabilities, unsupported formats and reinit passed");
    return 0;
}
