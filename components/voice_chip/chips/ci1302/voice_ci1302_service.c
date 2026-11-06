#include <limits.h>
#include <string.h>
#include "oshal.h"
#include "voice_ci1302_service.h"
#include "ci1302_audio.h"

static voice_chip_service_record_start_cb_t record_start_cb;
static voice_chip_service_record_data_cb_t record_data_cb;
static voice_chip_service_record_stop_cb_t record_stop_cb;
static voice_chip_service_playback_cb_t play_start_cb, play_finish_cb;
static volatile int last_error;
static bool capture_requested;

static const voice_chip_caps_t caps = {
    .transport_flags = VOICE_CHIP_TRANSPORT_UART,
    .capability_flags = VOICE_CHIP_CAP_OUTPUT_VOLUME_PERCENT |
                        VOICE_CHIP_CAP_PCM_CAPTURE | VOICE_CHIP_CAP_PCM_PLAYBACK,
    .capture = {16000, 16, 1},
    .playback = {16000, 16, 1},
};

static void chip_event(ci1302_event_t event, const uint8_t *data,
                       uint16_t length, void *arg)
{
    uint8_t *owned;
    (void)arg;
    switch (event) {
    case CI1302_EVENT_ERROR:
        last_error = -1;
        break;
    case CI1302_EVENT_RECORD_START:
        if (capture_requested && record_start_cb) record_start_cb();
        break;
    case CI1302_EVENT_RECORD_DATA:
        if (!capture_requested || !record_data_cb || !length) break;
        owned = os_malloc(length);
        if (!owned) { last_error = -1; break; }
        memcpy(owned, data, length);
        record_data_cb(owned, length);
        break;
    case CI1302_EVENT_RECORD_STOP:
        capture_requested = false;
        if (record_stop_cb) record_stop_cb();
        break;
    case CI1302_EVENT_PLAY_START:
        if (play_start_cb) play_start_cb();
        break;
    case CI1302_EVENT_PLAY_STOP:
        if (!last_error && play_finish_cb) play_finish_cb();
        break;
    default:
        break;
    }
}

static int service_init(void)
{
    int ret = ci1302_audio_init();
    if (!ret) ci1302_audio_on_event(chip_event, NULL);
    return ret;
}

static int service_capture_start(void)
{
    int ret;
    if (!ci1302_is_ready() || ci1302_audio_status() || !record_data_cb) return -1;
    last_error = 0;
    capture_requested = true;
    /* Service consumes event PCM directly, not the synchronous read staging. */
    ret = ci1302_record_start();
    if (ret) { capture_requested = false; last_error = ret; }
    return ret;
}

static int service_capture_stop(void)
{
    int ret;
    capture_requested = false;
    ret = ci1302_record_stop();
    if (ret) last_error = ret;
    return ret;
}

static int service_play(const void *data, size_t len, int format)
{
    int ret;
    if (format != AUDIO_FORMAT_PCM || !data || !len || len > UINT_MAX ||
        (len & 1U) || !ci1302_is_ready() || ci1302_is_recording()) return -1;
    if (!ci1302_is_playing()) last_error = 0;
    if (last_error) return -1;
    ret = ci1302_audio_write(data, (unsigned int)len);
    if (ret) last_error = ret;
    return ret;
}

static int service_end(void)
{
    if (!ci1302_is_initialized() || !ci1302_is_playing() || last_error) return -1;
    ci1302_audio_drain();
    return 0;
}

static int service_cancel(void)
{
    ci1302_audio_cancel();
    return 0;
}

static int service_idle(void)
{
    int ret = 0;
    if (ci1302_is_recording()) ret = service_capture_stop();
    ci1302_audio_cancel();
    return ret;
}

static int service_deinit(void)
{
    /* Caller must stop/join its producer before closing. RX is joined by driver
     * before the callback pointers are released. */
    service_idle();
    if (ci1302_deinit() != 0) return -1;
    ci1302_audio_on_event(NULL, NULL);
    ci1302_set_platform_callback(NULL, NULL);
    record_start_cb = NULL;
    record_data_cb = NULL;
    record_stop_cb = NULL;
    play_start_cb = play_finish_cb = NULL;
    return 0;
}

static int service_volume(uint8_t percent)
{
    if (!percent || percent > 100) return -1;
    return ci1302_set_volume((uint8_t)(1U + (((unsigned int)percent - 1U) * 6U + 49U) / 99U));
}

static int service_get_volume(void)
{
    return 1 + ((int)ci1302_get_volume() - 1) * 99 / 6;
}

static int service_record_callbacks(voice_chip_service_record_start_cb_t start,
                                    voice_chip_service_record_data_cb_t data,
                                    voice_chip_service_record_stop_cb_t stop)
{
    if (ci1302_is_recording()) return -1;
    record_start_cb = start;
    record_data_cb = data;
    record_stop_cb = stop;
    return 0;
}

static int service_play_callbacks(voice_chip_service_playback_cb_t start,
                                  voice_chip_service_playback_cb_t stop)
{
    if (ci1302_is_playing()) return -1;
    play_start_cb = start;
    play_finish_cb = stop;
    return 0;
}

static const voice_chip_caps_t *service_caps(void) { return &caps; }
int voice_ci1302_service_last_error(void) { return last_error; }

static const voice_chip_service_ops_t ops = {
    .init = service_init,
    .deinit = service_deinit,
    .start_capture = service_capture_start,
    .stop_capture = service_capture_stop,
    .play_stream = service_play,
    .clear_stream = service_cancel,
    .cancel_playback = service_cancel,
    .set_output_volume = service_volume,
    .get_output_volume = service_get_volume,
    .set_idle = service_idle,
    .set_active = service_init,
    .get_status = ci1302_audio_status,
    .is_ready = ci1302_is_ready,
    .set_record_callbacks = service_record_callbacks,
    .set_playback_callbacks = service_play_callbacks,
    .get_caps = service_caps,
    .end_stream = service_end,
};

static const voice_chip_service_t service = {
    .name = "ci1302", .ops = &ops,
};

int voice_ci1302_service_register(void) { return voice_chip_service_register(&service); }
int voice_chip_service_register_default_backend(void) { return voice_ci1302_service_register(); }
