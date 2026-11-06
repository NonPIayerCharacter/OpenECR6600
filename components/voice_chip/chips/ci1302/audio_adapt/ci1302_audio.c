#include "ci1302_audio.h"
#include "ci1302_protocol.h"
#include "oshal.h"
#include <string.h>

/* One protocol frame for converting asynchronous UART capture to synchronous
 * hardware reads. This does not replace or resize any platform/CLI buffer. */
static uint8_t rx_frame[CI1302_PROTOCOL_MAX_PAYLOAD]
    __attribute__((section(".dma.data"), aligned(4)));
static size_t rx_head, rx_count;
static uint32_t rx_overflow;
static bool capturing, draining, stream_open;
static unsigned int generation;
static const uint8_t *tx_data;
static size_t tx_remaining;
static os_mutex_handle_t lock, reader, writer;
static ci1302_event_callback_t event_callback;
static void *event_user_data;

static void on_event(ci1302_event_t event, const uint8_t *data,
                     uint16_t length, void *arg)
{
    (void)arg;
    os_mutex_lock(lock, WAIT_FOREVER);
    if (event == CI1302_EVENT_RECORD_DATA && capturing) {
        for (size_t i = 0; i < length; ++i) {
            if (rx_count == sizeof(rx_frame)) {
                rx_overflow += length - i;
                break;
            }
            rx_frame[(rx_head + rx_count++) % sizeof(rx_frame)] = data[i];
        }
    } else if (event == CI1302_EVENT_RECORD_STOP) {
        capturing = false;
    } else if (event == CI1302_EVENT_ERROR) {
        ++generation;
        capturing = stream_open = false;
    } else if (event == CI1302_EVENT_PLAY_STOP) {
        stream_open = false;
    }
    os_mutex_unlock(lock);
    if (event_callback) event_callback(event, data, length, event_user_data);
}

static int pcm_source(uint8_t *out, uint32_t maximum, void *arg)
{
    size_t count;
    int result;
    (void)arg;
    os_mutex_lock(lock, WAIT_FOREVER);
    count = tx_remaining < maximum ? tx_remaining : maximum;
    if (count) {
        memcpy(out, tx_data, count);
        tx_data += count;
        tx_remaining -= count;
        result = (int)count;
    } else {
        result = draining ? 0 : CI1302_PCM_AGAIN;
    }
    os_mutex_unlock(lock);
    return result;
}

int ci1302_audio_init(void)
{
    if (!lock) {
        lock = os_mutex_create();
        reader = os_mutex_create();
        writer = os_mutex_create();
        if (!lock || !reader || !writer) {
            if (lock) os_mutex_destroy(lock);
            if (reader) os_mutex_destroy(reader);
            if (writer) os_mutex_destroy(writer);
            lock = reader = writer = NULL;
            return -1;
        }
    }
    if (ci1302_init() != 0) return -1;
    ci1302_set_platform_callback(on_event, NULL);
    return 0;
}

void ci1302_audio_on_event(ci1302_event_callback_t callback, void *arg)
{
    event_user_data = arg;
    event_callback = callback;
}

int ci1302_audio_capture_begin(void)
{
    if (ci1302_audio_init() != 0) return -1;
    os_mutex_lock(lock, WAIT_FOREVER);
    rx_head = rx_count = rx_overflow = 0;
    capturing = true;
    os_mutex_unlock(lock);
    if (ci1302_record_start() == 0) return 0;
    capturing = false;
    return -1;
}

void ci1302_audio_capture_stop(void)
{
    if (!lock) return;
    os_mutex_lock(lock, WAIT_FOREVER);
    capturing = false;
    os_mutex_unlock(lock);
    (void)ci1302_record_stop();
}

int ci1302_audio_read(void *data, unsigned int length)
{
    size_t copied = 0;
    unsigned int wait = 0;
    uint8_t *out = data;
    if (!lock || !data || !length) return -1;
    os_mutex_lock(reader, WAIT_FOREVER);
    while (copied < length && wait < 2000) {
        bool active;
        os_mutex_lock(lock, WAIT_FOREVER);
        while (rx_count && copied < length) {
            out[copied++] = rx_frame[rx_head];
            rx_head = (rx_head + 1) % sizeof(rx_frame);
            --rx_count;
        }
        active = capturing;
        os_mutex_unlock(lock);
        if (!active || !ci1302_is_initialized()) break;
        if (copied < length) { os_msleep(1); ++wait; }
    }
    /* Legacy hardware callers expect a full read; stop/timeout gets silence,
     * never stale heap bytes. Their existing stop flags govern task exit. */
    if (copied < length) memset(out + copied, 0, length - copied);
    os_mutex_unlock(reader);
    return copied == length && !rx_overflow ? 0 : -1;
}

int ci1302_audio_write(const void *data, unsigned int length)
{
    unsigned int current, wait = 0;
    int ret = -1;
    if (!data || !length || (length & 1U) || ci1302_audio_init() != 0) return -1;
    os_mutex_lock(writer, WAIT_FOREVER);
    /* Wait for a previously drained session's chip completion. */
    while (draining && ci1302_is_playing() && wait++ < 2000) os_msleep(1);
    if (draining && ci1302_is_playing()) goto release;
    os_mutex_lock(lock, WAIT_FOREVER);
    current = generation;
    draining = false;
    tx_data = data;
    tx_remaining = length;
    os_mutex_unlock(lock);
    if (!ci1302_is_playing()) stream_open = false;
    if (!stream_open) {
        if (ci1302_play_pcm_stream_start(pcm_source, NULL) != 0) goto release;
        stream_open = true;
    }
    wait = 0;
    while (wait++ < 2000) {
        size_t remaining;
        os_mutex_lock(lock, WAIT_FOREVER);
        remaining = tx_remaining;
        if (generation != current) {
            os_mutex_unlock(lock);
            break;
        }
        os_mutex_unlock(lock);
        if (!remaining) { ret = 0; break; }
        if (!ci1302_is_initialized() || !ci1302_is_playing()) break;
        os_msleep(1);
    }
release:
    os_mutex_lock(lock, WAIT_FOREVER);
    tx_data = NULL;
    tx_remaining = 0;
    os_mutex_unlock(lock);
    if (ret) ci1302_audio_cancel();
    os_mutex_unlock(writer);
    return ret;
}

void ci1302_audio_drain(void)
{
    if (!lock) return;
    os_mutex_lock(lock, WAIT_FOREVER);
    if (stream_open) draining = true;
    os_mutex_unlock(lock);
}

void ci1302_audio_cancel(void)
{
    if (!lock) return;
    os_mutex_lock(lock, WAIT_FOREVER);
    ++generation;
    tx_data = NULL;
    tx_remaining = 0;
    draining = false;
    stream_open = false;
    os_mutex_unlock(lock);
    (void)ci1302_play_stop();
}

int ci1302_audio_status(void)
{
    return (ci1302_is_recording() ? 1 : 0) | (ci1302_is_playing() ? 2 : 0);
}

uint32_t ci1302_audio_rx_overflow(void)
{
    return rx_overflow;
}
